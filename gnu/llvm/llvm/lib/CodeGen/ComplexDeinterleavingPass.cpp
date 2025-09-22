//===- ComplexDeinterleavingPass.cpp --------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Identification:
// This step is responsible for finding the patterns that can be lowered to
// complex instructions, and building a graph to represent the complex
// structures. Starting from the "Converging Shuffle" (a shuffle that
// reinterleaves the complex components, with a mask of <0, 2, 1, 3>), the
// operands are evaluated and identified as "Composite Nodes" (collections of
// instructions that can potentially be lowered to a single complex
// instruction). This is performed by checking the real and imaginary components
// and tracking the data flow for each component while following the operand
// pairs. Validity of each node is expected to be done upon creation, and any
// validation errors should halt traversal and prevent further graph
// construction.
// Instead of relying on Shuffle operations, vector interleaving and
// deinterleaving can be represented by vector.interleave2 and
// vector.deinterleave2 intrinsics. Scalable vectors can be represented only by
// these intrinsics, whereas, fixed-width vectors are recognized for both
// shufflevector instruction and intrinsics.
//
// Replacement:
// This step traverses the graph built up by identification, delegating to the
// target to validate and generate the correct intrinsics, and plumbs them
// together connecting each end of the new intrinsics graph to the existing
// use-def chain. This step is assumed to finish successfully, as all
// information is expected to be correct by this point.
//
//
// Internal data structure:
// ComplexDeinterleavingGraph:
// Keeps references to all the valid CompositeNodes formed as part of the
// transformation, and every Instruction contained within said nodes. It also
// holds onto a reference to the root Instruction, and the root node that should
// replace it.
//
// ComplexDeinterleavingCompositeNode:
// A CompositeNode represents a single transformation point; each node should
// transform into a single complex instruction (ignoring vector splitting, which
// would generate more instructions per node). They are identified in a
// depth-first manner, traversing and identifying the operands of each
// instruction in the order they appear in the IR.
// Each node maintains a reference  to its Real and Imaginary instructions,
// as well as any additional instructions that make up the identified operation
// (Internal instructions should only have uses within their containing node).
// A Node also contains the rotation and operation type that it represents.
// Operands contains pointers to other CompositeNodes, acting as the edges in
// the graph. ReplacementValue is the transformed Value* that has been emitted
// to the IR.
//
// Note: If the operation of a Node is Shuffle, only the Real, Imaginary, and
// ReplacementValue fields of that Node are relevant, where the ReplacementValue
// should be pre-populated.
//
//===----------------------------------------------------------------------===//

#include "llvm/CodeGen/ComplexDeinterleavingPass.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/Analysis/TargetTransformInfo.h"
#include "llvm/CodeGen/TargetLowering.h"
#include "llvm/CodeGen/TargetPassConfig.h"
#include "llvm/CodeGen/TargetSubtargetInfo.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PatternMatch.h"
#include "llvm/InitializePasses.h"
#include "llvm/Target/TargetMachine.h"
#include "llvm/Transforms/Utils/Local.h"
#include <algorithm>

using namespace llvm;
using namespace PatternMatch;

#define DEBUG_TYPE "complex-deinterleaving"

STATISTIC(NumComplexTransformations, "Amount of complex patterns transformed");

static cl::opt<bool> ComplexDeinterleavingEnabled(
    "enable-complex-deinterleaving",
    cl::desc("Enable generation of complex instructions"), cl::init(true),
    cl::Hidden);

/// Checks the given mask, and determines whether said mask is interleaving.
///
/// To be interleaving, a mask must alternate between `i` and `i + (Length /
/// 2)`, and must contain all numbers within the range of `[0..Length)` (e.g. a
/// 4x vector interleaving mask would be <0, 2, 1, 3>).
static bool isInterleavingMask(ArrayRef<int> Mask);

/// Checks the given mask, and determines whether said mask is deinterleaving.
///
/// To be deinterleaving, a mask must increment in steps of 2, and either start
/// with 0 or 1.
/// (e.g. an 8x vector deinterleaving mask would be either <0, 2, 4, 6> or
/// <1, 3, 5, 7>).
static bool isDeinterleavingMask(ArrayRef<int> Mask);

/// Returns true if the operation is a negation of V, and it works for both
/// integers and floats.
static bool isNeg(Value *V);

/// Returns the operand for negation operation.
static Value *getNegOperand(Value *V);

namespace {

class ComplexDeinterleavingLegacyPass : public FunctionPass {
public:
  static char ID;

  ComplexDeinterleavingLegacyPass(const TargetMachine *TM = nullptr)
      : FunctionPass(ID), TM(TM) {
    initializeComplexDeinterleavingLegacyPassPass(
        *PassRegistry::getPassRegistry());
  }

  StringRef getPassName() const override {
    return "Complex Deinterleaving Pass";
  }

  bool runOnFunction(Function &F) override;
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.addRequired<TargetLibraryInfoWrapperPass>();
    AU.setPreservesCFG();
  }

private:
  const TargetMachine *TM;
};

class ComplexDeinterleavingGraph;
struct ComplexDeinterleavingCompositeNode {

  ComplexDeinterleavingCompositeNode(ComplexDeinterleavingOperation Op,
                                     Value *R, Value *I)
      : Operation(Op), Real(R), Imag(I) {}

private:
  friend class ComplexDeinterleavingGraph;
  using NodePtr = std::shared_ptr<ComplexDeinterleavingCompositeNode>;
  using RawNodePtr = ComplexDeinterleavingCompositeNode *;

public:
  ComplexDeinterleavingOperation Operation;
  Value *Real;
  Value *Imag;

  // This two members are required exclusively for generating
  // ComplexDeinterleavingOperation::Symmetric operations.
  unsigned Opcode;
  std::optional<FastMathFlags> Flags;

  ComplexDeinterleavingRotation Rotation =
      ComplexDeinterleavingRotation::Rotation_0;
  SmallVector<RawNodePtr> Operands;
  Value *ReplacementNode = nullptr;

  void addOperand(NodePtr Node) { Operands.push_back(Node.get()); }

  void dump() { dump(dbgs()); }
  void dump(raw_ostream &OS) {
    auto PrintValue = [&](Value *V) {
      if (V) {
        OS << "\"";
        V->print(OS, true);
        OS << "\"\n";
      } else
        OS << "nullptr\n";
    };
    auto PrintNodeRef = [&](RawNodePtr Ptr) {
      if (Ptr)
        OS << Ptr << "\n";
      else
        OS << "nullptr\n";
    };

    OS << "- CompositeNode: " << this << "\n";
    OS << "  Real: ";
    PrintValue(Real);
    OS << "  Imag: ";
    PrintValue(Imag);
    OS << "  ReplacementNode: ";
    PrintValue(ReplacementNode);
    OS << "  Operation: " << (int)Operation << "\n";
    OS << "  Rotation: " << ((int)Rotation * 90) << "\n";
    OS << "  Operands: \n";
    for (const auto &Op : Operands) {
      OS << "    - ";
      PrintNodeRef(Op);
    }
  }
};

class ComplexDeinterleavingGraph {
public:
  struct Product {
    Value *Multiplier;
    Value *Multiplicand;
    bool IsPositive;
  };

  using Addend = std::pair<Value *, bool>;
  using NodePtr = ComplexDeinterleavingCompositeNode::NodePtr;
  using RawNodePtr = ComplexDeinterleavingCompositeNode::RawNodePtr;

  // Helper struct for holding info about potential partial multiplication
  // candidates
  struct PartialMulCandidate {
    Value *Common;
    NodePtr Node;
    unsigned RealIdx;
    unsigned ImagIdx;
    bool IsNodeInverted;
  };

  explicit ComplexDeinterleavingGraph(const TargetLowering *TL,
                                      const TargetLibraryInfo *TLI)
      : TL(TL), TLI(TLI) {}

private:
  const TargetLowering *TL = nullptr;
  const TargetLibraryInfo *TLI = nullptr;
  SmallVector<NodePtr> CompositeNodes;
  DenseMap<std::pair<Value *, Value *>, NodePtr> CachedResult;

  SmallPtrSet<Instruction *, 16> FinalInstructions;

  /// Root instructions are instructions from which complex computation starts
  std::map<Instruction *, NodePtr> RootToNode;

  /// Topologically sorted root instructions
  SmallVector<Instruction *, 1> OrderedRoots;

  /// When examining a basic block for complex deinterleaving, if it is a simple
  /// one-block loop, then the only incoming block is 'Incoming' and the
  /// 'BackEdge' block is the block itself."
  BasicBlock *BackEdge = nullptr;
  BasicBlock *Incoming = nullptr;

  /// ReductionInfo maps from %ReductionOp to %PHInode and Instruction
  /// %OutsideUser as it is shown in the IR:
  ///
  /// vector.body:
  ///   %PHInode = phi <vector type> [ zeroinitializer, %entry ],
  ///                                [ %ReductionOp, %vector.body ]
  ///   ...
  ///   %ReductionOp = fadd i64 ...
  ///   ...
  ///   br i1 %condition, label %vector.body, %middle.block
  ///
  /// middle.block:
  ///   %OutsideUser = llvm.vector.reduce.fadd(..., %ReductionOp)
  ///
  /// %OutsideUser can be `llvm.vector.reduce.fadd` or `fadd` preceding
  /// `llvm.vector.reduce.fadd` when unroll factor isn't one.
  MapVector<Instruction *, std::pair<PHINode *, Instruction *>> ReductionInfo;

  /// In the process of detecting a reduction, we consider a pair of
  /// %ReductionOP, which we refer to as real and imag (or vice versa), and
  /// traverse the use-tree to detect complex operations. As this is a reduction
  /// operation, it will eventually reach RealPHI and ImagPHI, which corresponds
  /// to the %ReductionOPs that we suspect to be complex.
  /// RealPHI and ImagPHI are used by the identifyPHINode method.
  PHINode *RealPHI = nullptr;
  PHINode *ImagPHI = nullptr;

  /// Set this flag to true if RealPHI and ImagPHI were reached during reduction
  /// detection.
  bool PHIsFound = false;

  /// OldToNewPHI maps the original real PHINode to a new, double-sized PHINode.
  /// The new PHINode corresponds to a vector of deinterleaved complex numbers.
  /// This mapping is populated during
  /// ComplexDeinterleavingOperation::ReductionPHI node replacement. It is then
  /// used in the ComplexDeinterleavingOperation::ReductionOperation node
  /// replacement process.
  std::map<PHINode *, PHINode *> OldToNewPHI;

  NodePtr prepareCompositeNode(ComplexDeinterleavingOperation Operation,
                               Value *R, Value *I) {
    assert(((Operation != ComplexDeinterleavingOperation::ReductionPHI &&
             Operation != ComplexDeinterleavingOperation::ReductionOperation) ||
            (R && I)) &&
           "Reduction related nodes must have Real and Imaginary parts");
    return std::make_shared<ComplexDeinterleavingCompositeNode>(Operation, R,
                                                                I);
  }

  NodePtr submitCompositeNode(NodePtr Node) {
    CompositeNodes.push_back(Node);
    if (Node->Real && Node->Imag)
      CachedResult[{Node->Real, Node->Imag}] = Node;
    return Node;
  }

  /// Identifies a complex partial multiply pattern and its rotation, based on
  /// the following patterns
  ///
  ///  0:  r: cr + ar * br
  ///      i: ci + ar * bi
  /// 90:  r: cr - ai * bi
  ///      i: ci + ai * br
  /// 180: r: cr - ar * br
  ///      i: ci - ar * bi
  /// 270: r: cr + ai * bi
  ///      i: ci - ai * br
  NodePtr identifyPartialMul(Instruction *Real, Instruction *Imag);

  /// Identify the other branch of a Partial Mul, taking the CommonOperandI that
  /// is partially known from identifyPartialMul, filling in the other half of
  /// the complex pair.
  NodePtr
  identifyNodeWithImplicitAdd(Instruction *I, Instruction *J,
                              std::pair<Value *, Value *> &CommonOperandI);

  /// Identifies a complex add pattern and its rotation, based on the following
  /// patterns.
  ///
  /// 90:  r: ar - bi
  ///      i: ai + br
  /// 270: r: ar + bi
  ///      i: ai - br
  NodePtr identifyAdd(Instruction *Real, Instruction *Imag);
  NodePtr identifySymmetricOperation(Instruction *Real, Instruction *Imag);

  NodePtr identifyNode(Value *R, Value *I);

  /// Determine if a sum of complex numbers can be formed from \p RealAddends
  /// and \p ImagAddens. If \p Accumulator is not null, add the result to it.
  /// Return nullptr if it is not possible to construct a complex number.
  /// \p Flags are needed to generate symmetric Add and Sub operations.
  NodePtr identifyAdditions(std::list<Addend> &RealAddends,
                            std::list<Addend> &ImagAddends,
                            std::optional<FastMathFlags> Flags,
                            NodePtr Accumulator);

  /// Extract one addend that have both real and imaginary parts positive.
  NodePtr extractPositiveAddend(std::list<Addend> &RealAddends,
                                std::list<Addend> &ImagAddends);

  /// Determine if sum of multiplications of complex numbers can be formed from
  /// \p RealMuls and \p ImagMuls. If \p Accumulator is not null, add the result
  /// to it. Return nullptr if it is not possible to construct a complex number.
  NodePtr identifyMultiplications(std::vector<Product> &RealMuls,
                                  std::vector<Product> &ImagMuls,
                                  NodePtr Accumulator);

  /// Go through pairs of multiplication (one Real and one Imag) and find all
  /// possible candidates for partial multiplication and put them into \p
  /// Candidates. Returns true if all Product has pair with common operand
  bool collectPartialMuls(const std::vector<Product> &RealMuls,
                          const std::vector<Product> &ImagMuls,
                          std::vector<PartialMulCandidate> &Candidates);

  /// If the code is compiled with -Ofast or expressions have `reassoc` flag,
  /// the order of complex computation operations may be significantly altered,
  /// and the real and imaginary parts may not be executed in parallel. This
  /// function takes this into consideration and employs a more general approach
  /// to identify complex computations. Initially, it gathers all the addends
  /// and multiplicands and then constructs a complex expression from them.
  NodePtr identifyReassocNodes(Instruction *I, Instruction *J);

  NodePtr identifyRoot(Instruction *I);

  /// Identifies the Deinterleave operation applied to a vector containing
  /// complex numbers. There are two ways to represent the Deinterleave
  /// operation:
  /// * Using two shufflevectors with even indices for /pReal instruction and
  /// odd indices for /pImag instructions (only for fixed-width vectors)
  /// * Using two extractvalue instructions applied to `vector.deinterleave2`
  /// intrinsic (for both fixed and scalable vectors)
  NodePtr identifyDeinterleave(Instruction *Real, Instruction *Imag);

  /// identifying the operation that represents a complex number repeated in a
  /// Splat vector. There are two possible types of splats: ConstantExpr with
  /// the opcode ShuffleVector and ShuffleVectorInstr. Both should have an
  /// initialization mask with all values set to zero.
  NodePtr identifySplat(Value *Real, Value *Imag);

  NodePtr identifyPHINode(Instruction *Real, Instruction *Imag);

  /// Identifies SelectInsts in a loop that has reduction with predication masks
  /// and/or predicated tail folding
  NodePtr identifySelectNode(Instruction *Real, Instruction *Imag);

  Value *replaceNode(IRBuilderBase &Builder, RawNodePtr Node);

  /// Complete IR modifications after producing new reduction operation:
  /// * Populate the PHINode generated for
  /// ComplexDeinterleavingOperation::ReductionPHI
  /// * Deinterleave the final value outside of the loop and repurpose original
  /// reduction users
  void processReductionOperation(Value *OperationReplacement, RawNodePtr Node);

public:
  void dump() { dump(dbgs()); }
  void dump(raw_ostream &OS) {
    for (const auto &Node : CompositeNodes)
      Node->dump(OS);
  }

  /// Returns false if the deinterleaving operation should be cancelled for the
  /// current graph.
  bool identifyNodes(Instruction *RootI);

  /// In case \pB is one-block loop, this function seeks potential reductions
  /// and populates ReductionInfo. Returns true if any reductions were
  /// identified.
  bool collectPotentialReductions(BasicBlock *B);

  void identifyReductionNodes();

  /// Check that every instruction, from the roots to the leaves, has internal
  /// uses.
  bool checkNodes();

  /// Perform the actual replacement of the underlying instruction graph.
  void replaceNodes();
};

class ComplexDeinterleaving {
public:
  ComplexDeinterleaving(const TargetLowering *tl, const TargetLibraryInfo *tli)
      : TL(tl), TLI(tli) {}
  bool runOnFunction(Function &F);

private:
  bool evaluateBasicBlock(BasicBlock *B);

  const TargetLowering *TL = nullptr;
  const TargetLibraryInfo *TLI = nullptr;
};

} // namespace

char ComplexDeinterleavingLegacyPass::ID = 0;

INITIALIZE_PASS_BEGIN(ComplexDeinterleavingLegacyPass, DEBUG_TYPE,
                      "Complex Deinterleaving", false, false)
INITIALIZE_PASS_END(ComplexDeinterleavingLegacyPass, DEBUG_TYPE,
                    "Complex Deinterleaving", false, false)

PreservedAnalyses ComplexDeinterleavingPass::run(Function &F,
                                                 FunctionAnalysisManager &AM) {
  const TargetLowering *TL = TM->getSubtargetImpl(F)->getTargetLowering();
  auto &TLI = AM.getResult<llvm::TargetLibraryAnalysis>(F);
  if (!ComplexDeinterleaving(TL, &TLI).runOnFunction(F))
    return PreservedAnalyses::all();

  PreservedAnalyses PA;
  PA.preserve<FunctionAnalysisManagerModuleProxy>();
  return PA;
}

FunctionPass *llvm::createComplexDeinterleavingPass(const TargetMachine *TM) {
  return new ComplexDeinterleavingLegacyPass(TM);
}

bool ComplexDeinterleavingLegacyPass::runOnFunction(Function &F) {
  const auto *TL = TM->getSubtargetImpl(F)->getTargetLowering();
  auto TLI = getAnalysis<TargetLibraryInfoWrapperPass>().getTLI(F);
  return ComplexDeinterleaving(TL, &TLI).runOnFunction(F);
}

bool ComplexDeinterleaving::runOnFunction(Function &F) {
  if (!ComplexDeinterleavingEnabled) {
    LLVM_DEBUG(
        dbgs() << "Complex deinterleaving has been explicitly disabled.\n");
    return false;
  }

  if (!TL->isComplexDeinterleavingSupported()) {
    LLVM_DEBUG(
        dbgs() << "Complex deinterleaving has been disabled, target does "
                  "not support lowering of complex number operations.\n");
    return false;
  }

  bool Changed = false;
  for (auto &B : F)
    Changed |= evaluateBasicBlock(&B);

  return Changed;
}

static bool isInterleavingMask(ArrayRef<int> Mask) {
  // If the size is not even, it's not an interleaving mask
  if ((Mask.size() & 1))
    return false;

  int HalfNumElements = Mask.size() / 2;
  for (int Idx = 0; Idx < HalfNumElements; ++Idx) {
    int MaskIdx = Idx * 2;
    if (Mask[MaskIdx] != Idx || Mask[MaskIdx + 1] != (Idx + HalfNumElements))
      return false;
  }

  return true;
}

static bool isDeinterleavingMask(ArrayRef<int> Mask) {
  int Offset = Mask[0];
  int HalfNumElements = Mask.size() / 2;

  for (int Idx = 1; Idx < HalfNumElements; ++Idx) {
    if (Mask[Idx] != (Idx * 2) + Offset)
      return false;
  }

  return true;
}

bool isNeg(Value *V) {
  return match(V, m_FNeg(m_Value())) || match(V, m_Neg(m_Value()));
}

Value *getNegOperand(Value *V) {
  assert(isNeg(V));
  auto *I = cast<Instruction>(V);
  if (I->getOpcode() == Instruction::FNeg)
    return I->getOperand(0);

  return I->getOperand(1);
}

bool ComplexDeinterleaving::evaluateBasicBlock(BasicBlock *B) {
  ComplexDeinterleavingGraph Graph(TL, TLI);
  if (Graph.collectPotentialReductions(B))
    Graph.identifyReductionNodes();

  for (auto &I : *B)
    Graph.identifyNodes(&I);

  if (Graph.checkNodes()) {
    Graph.replaceNodes();
    return true;
  }

  return false;
}

ComplexDeinterleavingGraph::NodePtr
ComplexDeinterleavingGraph::identifyNodeWithImplicitAdd(
    Instruction *Real, Instruction *Imag,
    std::pair<Value *, Value *> &PartialMatch) {
  LLVM_DEBUG(dbgs() << "identifyNodeWithImplicitAdd " << *Real << " / " << *Imag
                    << "\n");

  if (!Real->hasOneUse() || !Imag->hasOneUse()) {
    LLVM_DEBUG(dbgs() << "  - Mul operand has multiple uses.\n");
    return nullptr;
  }

  if ((Real->getOpcode() != Instruction::FMul &&
       Real->getOpcode() != Instruction::Mul) ||
      (Imag->getOpcode() != Instruction::FMul &&
       Imag->getOpcode() != Instruction::Mul)) {
    LLVM_DEBUG(
        dbgs() << "  - Real or imaginary instruction is not fmul or mul\n");
    return nullptr;
  }

  Value *R0 = Real->getOperand(0);
  Value *R1 = Real->getOperand(1);
  Value *I0 = Imag->getOperand(0);
  Value *I1 = Imag->getOperand(1);

  // A +/+ has a rotation of 0. If any of the operands are fneg, we flip the
  // rotations and use the operand.
  unsigned Negs = 0;
  Value *Op;
  if (match(R0, m_Neg(m_Value(Op)))) {
    Negs |= 1;
    R0 = Op;
  } else if (match(R1, m_Neg(m_Value(Op)))) {
    Negs |= 1;
    R1 = Op;
  }

  if (isNeg(I0)) {
    Negs |= 2;
    Negs ^= 1;
    I0 = Op;
  } else if (match(I1, m_Neg(m_Value(Op)))) {
    Negs |= 2;
    Negs ^= 1;
    I1 = Op;
  }

  ComplexDeinterleavingRotation Rotation = (ComplexDeinterleavingRotation)Negs;

  Value *CommonOperand;
  Value *UncommonRealOp;
  Value *UncommonImagOp;

  if (R0 == I0 || R0 == I1) {
    CommonOperand = R0;
    UncommonRealOp = R1;
  } else if (R1 == I0 || R1 == I1) {
    CommonOperand = R1;
    UncommonRealOp = R0;
  } else {
    LLVM_DEBUG(dbgs() << "  - No equal operand\n");
    return nullptr;
  }

  UncommonImagOp = (CommonOperand == I0) ? I1 : I0;
  if (Rotation == ComplexDeinterleavingRotation::Rotation_90 ||
      Rotation == ComplexDeinterleavingRotation::Rotation_270)
    std::swap(UncommonRealOp, UncommonImagOp);

  // Between identifyPartialMul and here we need to have found a complete valid
  // pair from the CommonOperand of each part.
  if (Rotation == ComplexDeinterleavingRotation::Rotation_0 ||
      Rotation == ComplexDeinterleavingRotation::Rotation_180)
    PartialMatch.first = CommonOperand;
  else
    PartialMatch.second = CommonOperand;

  if (!PartialMatch.first || !PartialMatch.second) {
    LLVM_DEBUG(dbgs() << "  - Incomplete partial match\n");
    return nullptr;
  }

  NodePtr CommonNode = identifyNode(PartialMatch.first, PartialMatch.second);
  if (!CommonNode) {
    LLVM_DEBUG(dbgs() << "  - No CommonNode identified\n");
    return nullptr;
  }

  NodePtr UncommonNode = identifyNode(UncommonRealOp, UncommonImagOp);
  if (!UncommonNode) {
    LLVM_DEBUG(dbgs() << "  - No UncommonNode identified\n");
    return nullptr;
  }

  NodePtr Node = prepareCompositeNode(
      ComplexDeinterleavingOperation::CMulPartial, Real, Imag);
  Node->Rotation = Rotation;
  Node->addOperand(CommonNode);
  Node->addOperand(UncommonNode);
  return submitCompositeNode(Node);
}

ComplexDeinterleavingGraph::NodePtr
ComplexDeinterleavingGraph::identifyPartialMul(Instruction *Real,
                                               Instruction *Imag) {
  LLVM_DEBUG(dbgs() << "identifyPartialMul " << *Real << " / " << *Imag
                    << "\n");
  // Determine rotation
  auto IsAdd = [](unsigned Op) {
    return Op == Instruction::FAdd || Op == Instruction::Add;
  };
  auto IsSub = [](unsigned Op) {
    return Op == Instruction::FSub || Op == Instruction::Sub;
  };
  ComplexDeinterleavingRotation Rotation;
  if (IsAdd(Real->getOpcode()) && IsAdd(Imag->getOpcode()))
    Rotation = ComplexDeinterleavingRotation::Rotation_0;
  else if (IsSub(Real->getOpcode()) && IsAdd(Imag->getOpcode()))
    Rotation = ComplexDeinterleavingRotation::Rotation_90;
  else if (IsSub(Real->getOpcode()) && IsSub(Imag->getOpcode()))
    Rotation = ComplexDeinterleavingRotation::Rotation_180;
  else if (IsAdd(Real->getOpcode()) && IsSub(Imag->getOpcode()))
    Rotation = ComplexDeinterleavingRotation::Rotation_270;
  else {
    LLVM_DEBUG(dbgs() << "  - Unhandled rotation.\n");
    return nullptr;
  }

  if (isa<FPMathOperator>(Real) &&
      (!Real->getFastMathFlags().allowContract() ||
       !Imag->getFastMathFlags().allowContract())) {
    LLVM_DEBUG(dbgs() << "  - Contract is missing from the FastMath flags.\n");
    return nullptr;
  }

  Value *CR = Real->getOperand(0);
  Instruction *RealMulI = dyn_cast<Instruction>(Real->getOperand(1));
  if (!RealMulI)
    return nullptr;
  Value *CI = Imag->getOperand(0);
  Instruction *ImagMulI = dyn_cast<Instruction>(Imag->getOperand(1));
  if (!ImagMulI)
    return nullptr;

  if (!RealMulI->hasOneUse() || !ImagMulI->hasOneUse()) {
    LLVM_DEBUG(dbgs() << "  - Mul instruction has multiple uses\n");
    return nullptr;
  }

  Value *R0 = RealMulI->getOperand(0);
  Value *R1 = RealMulI->getOperand(1);
  Value *I0 = ImagMulI->getOperand(0);
  Value *I1 = ImagMulI->getOperand(1);

  Value *CommonOperand;
  Value *UncommonRealOp;
  Value *UncommonImagOp;

  if (R0 == I0 || R0 == I1) {
    CommonOperand = R0;
    UncommonRealOp = R1;
  } else if (R1 == I0 || R1 == I1) {
    CommonOperand = R1;
    UncommonRealOp = R0;
  } else {
    LLVM_DEBUG(dbgs() << "  - No equal operand\n");
    return nullptr;
  }

  UncommonImagOp = (CommonOperand == I0) ? I1 : I0;
  if (Rotation == ComplexDeinterleavingRotation::Rotation_90 ||
      Rotation == ComplexDeinterleavingRotation::Rotation_270)
    std::swap(UncommonRealOp, UncommonImagOp);

  std::pair<Value *, Value *> PartialMatch(
      (Rotation == ComplexDeinterleavingRotation::Rotation_0 ||
       Rotation == ComplexDeinterleavingRotation::Rotation_180)
          ? CommonOperand
          : nullptr,
      (Rotation == ComplexDeinterleavingRotation::Rotation_90 ||
       Rotation == ComplexDeinterleavingRotation::Rotation_270)
          ? CommonOperand
          : nullptr);

  auto *CRInst = dyn_cast<Instruction>(CR);
  auto *CIInst = dyn_cast<Instruction>(CI);

  if (!CRInst || !CIInst) {
    LLVM_DEBUG(dbgs() << "  - Common operands are not instructions.\n");
    return nullptr;
  }

  NodePtr CNode = identifyNodeWithImplicitAdd(CRInst, CIInst, PartialMatch);
  if (!CNode) {
    LLVM_DEBUG(dbgs() << "  - No cnode identified\n");
    return nullptr;
  }

  NodePtr UncommonRes = identifyNode(UncommonRealOp, UncommonImagOp);
  if (!UncommonRes) {
    LLVM_DEBUG(dbgs() << "  - No UncommonRes identified\n");
    return nullptr;
  }

  assert(PartialMatch.first && PartialMatch.second);
  NodePtr CommonRes = identifyNode(PartialMatch.first, PartialMatch.second);
  if (!CommonRes) {
    LLVM_DEBUG(dbgs() << "  - No CommonRes identified\n");
    return nullptr;
  }

  NodePtr Node = prepareCompositeNode(
      ComplexDeinterleavingOperation::CMulPartial, Real, Imag);
  Node->Rotation = Rotation;
  Node->addOperand(CommonRes);
  Node->addOperand(UncommonRes);
  Node->addOperand(CNode);
  return submitCompositeNode(Node);
}

ComplexDeinterleavingGraph::NodePtr
ComplexDeinterleavingGraph::identifyAdd(Instruction *Real, Instruction *Imag) {
  LLVM_DEBUG(dbgs() << "identifyAdd " << *Real << " / " << *Imag << "\n");

  // Determine rotation
  ComplexDeinterleavingRotation Rotation;
  if ((Real->getOpcode() == Instruction::FSub &&
       Imag->getOpcode() == Instruction::FAdd) ||
      (Real->getOpcode() == Instruction::Sub &&
       Imag->getOpcode() == Instruction::Add))
    Rotation = ComplexDeinterleavingRotation::Rotation_90;
  else if ((Real->getOpcode() == Instruction::FAdd &&
            Imag->getOpcode() == Instruction::FSub) ||
           (Real->getOpcode() == Instruction::Add &&
            Imag->getOpcode() == Instruction::Sub))
    Rotation = ComplexDeinterleavingRotation::Rotation_270;
  else {
    LLVM_DEBUG(dbgs() << " - Unhandled case, rotation is not assigned.\n");
    return nullptr;
  }

  auto *AR = dyn_cast<Instruction>(Real->getOperand(0));
  auto *BI = dyn_cast<Instruction>(Real->getOperand(1));
  auto *AI = dyn_cast<Instruction>(Imag->getOperand(0));
  auto *BR = dyn_cast<Instruction>(Imag->getOperand(1));

  if (!AR || !AI || !BR || !BI) {
    LLVM_DEBUG(dbgs() << " - Not all operands are instructions.\n");
    return nullptr;
  }

  NodePtr ResA = identifyNode(AR, AI);
  if (!ResA) {
    LLVM_DEBUG(dbgs() << " - AR/AI is not identified as a composite node.\n");
    return nullptr;
  }
  NodePtr ResB = identifyNode(BR, BI);
  if (!ResB) {
    LLVM_DEBUG(dbgs() << " - BR/BI is not identified as a composite node.\n");
    return nullptr;
  }

  NodePtr Node =
      prepareCompositeNode(ComplexDeinterleavingOperation::CAdd, Real, Imag);
  Node->Rotation = Rotation;
  Node->addOperand(ResA);
  Node->addOperand(ResB);
  return submitCompositeNode(Node);
}

static bool isInstructionPairAdd(Instruction *A, Instruction *B) {
  unsigned OpcA = A->getOpcode();
  unsigned OpcB = B->getOpcode();

  return (OpcA == Instruction::FSub && OpcB == Instruction::FAdd) ||
         (OpcA == Instruction::FAdd && OpcB == Instruction::FSub) ||
         (OpcA == Instruction::Sub && OpcB == Instruction::Add) ||
         (OpcA == Instruction::Add && OpcB == Instruction::Sub);
}

static bool isInstructionPairMul(Instruction *A, Instruction *B) {
  auto Pattern =
      m_BinOp(m_FMul(m_Value(), m_Value()), m_FMul(m_Value(), m_Value()));

  return match(A, Pattern) && match(B, Pattern);
}

static bool isInstructionPotentiallySymmetric(Instruction *I) {
  switch (I->getOpcode()) {
  case Instruction::FAdd:
  case Instruction::FSub:
  case Instruction::FMul:
  case Instruction::FNeg:
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
    return true;
  default:
    return false;
  }
}

ComplexDeinterleavingGraph::NodePtr
ComplexDeinterleavingGraph::identifySymmetricOperation(Instruction *Real,
                                                       Instruction *Imag) {
  if (Real->getOpcode() != Imag->getOpcode())
    return nullptr;

  if (!isInstructionPotentiallySymmetric(Real) ||
      !isInstructionPotentiallySymmetric(Imag))
    return nullptr;

  auto *R0 = Real->getOperand(0);
  auto *I0 = Imag->getOperand(0);

  NodePtr Op0 = identifyNode(R0, I0);
  NodePtr Op1 = nullptr;
  if (Op0 == nullptr)
    return nullptr;

  if (Real->isBinaryOp()) {
    auto *R1 = Real->getOperand(1);
    auto *I1 = Imag->getOperand(1);
    Op1 = identifyNode(R1, I1);
    if (Op1 == nullptr)
      return nullptr;
  }

  if (isa<FPMathOperator>(Real) &&
      Real->getFastMathFlags() != Imag->getFastMathFlags())
    return nullptr;

  auto Node = prepareCompositeNode(ComplexDeinterleavingOperation::Symmetric,
                                   Real, Imag);
  Node->Opcode = Real->getOpcode();
  if (isa<FPMathOperator>(Real))
    Node->Flags = Real->getFastMathFlags();

  Node->addOperand(Op0);
  if (Real->isBinaryOp())
    Node->addOperand(Op1);

  return submitCompositeNode(Node);
}

ComplexDeinterleavingGraph::NodePtr
ComplexDeinterleavingGraph::identifyNode(Value *R, Value *I) {
  LLVM_DEBUG(dbgs() << "identifyNode on " << *R << " / " << *I << "\n");
  assert(R->getType() == I->getType() &&
         "Real and imaginary parts should not have different types");

  auto It = CachedResult.find({R, I});
  if (It != CachedResult.end()) {
    LLVM_DEBUG(dbgs() << " - Folding to existing node\n");
    return It->second;
  }

  if (NodePtr CN = identifySplat(R, I))
    return CN;

  auto *Real = dyn_cast<Instruction>(R);
  auto *Imag = dyn_cast<Instruction>(I);
  if (!Real || !Imag)
    return nullptr;

  if (NodePtr CN = identifyDeinterleave(Real, Imag))
    return CN;

  if (NodePtr CN = identifyPHINode(Real, Imag))
    return CN;

  if (NodePtr CN = identifySelectNode(Real, Imag))
    return CN;

  auto *VTy = cast<VectorType>(Real->getType());
  auto *NewVTy = VectorType::getDoubleElementsVectorType(VTy);

  bool HasCMulSupport = TL->isComplexDeinterleavingOperationSupported(
      ComplexDeinterleavingOperation::CMulPartial, NewVTy);
  bool HasCAddSupport = TL->isComplexDeinterleavingOperationSupported(
      ComplexDeinterleavingOperation::CAdd, NewVTy);

  if (HasCMulSupport && isInstructionPairMul(Real, Imag)) {
    if (NodePtr CN = identifyPartialMul(Real, Imag))
      return CN;
  }

  if (HasCAddSupport && isInstructionPairAdd(Real, Imag)) {
    if (NodePtr CN = identifyAdd(Real, Imag))
      return CN;
  }

  if (HasCMulSupport && HasCAddSupport) {
    if (NodePtr CN = identifyReassocNodes(Real, Imag))
      return CN;
  }

  if (NodePtr CN = identifySymmetricOperation(Real, Imag))
    return CN;

  LLVM_DEBUG(dbgs() << "  - Not recognised as a valid pattern.\n");
  CachedResult[{R, I}] = nullptr;
  return nullptr;
}

ComplexDeinterleavingGraph::NodePtr
ComplexDeinterleavingGraph::identifyReassocNodes(Instruction *Real,
                                                 Instruction *Imag) {
  auto IsOperationSupported = [](unsigned Opcode) -> bool {
    return Opcode == Instruction::FAdd || Opcode == Instruction::FSub ||
           Opcode == Instruction::FNeg || Opcode == Instruction::Add ||
           Opcode == Instruction::Sub;
  };

  if (!IsOperationSupported(Real->getOpcode()) ||
      !IsOperationSupported(Imag->getOpcode()))
    return nullptr;

  std::optional<FastMathFlags> Flags;
  if (isa<FPMathOperator>(Real)) {
    if (Real->getFastMathFlags() != Imag->getFastMathFlags()) {
      LLVM_DEBUG(dbgs() << "The flags in Real and Imaginary instructions are "
                           "not identical\n");
      return nullptr;
    }

    Flags = Real->getFastMathFlags();
    if (!Flags->allowReassoc()) {
      LLVM_DEBUG(
          dbgs()
          << "the 'Reassoc' attribute is missing in the FastMath flags\n");
      return nullptr;
    }
  }

  // Collect multiplications and addend instructions from the given instruction
  // while traversing it operands. Additionally, verify that all instructions
  // have the same fast math flags.
  auto Collect = [&Flags](Instruction *Insn, std::vector<Product> &Muls,
                          std::list<Addend> &Addends) -> bool {
    SmallVector<PointerIntPair<Value *, 1, bool>> Worklist = {{Insn, true}};
    SmallPtrSet<Value *, 8> Visited;
    while (!Worklist.empty()) {
      auto [V, IsPositive] = Worklist.back();
      Worklist.pop_back();
      if (!Visited.insert(V).second)
        continue;

      Instruction *I = dyn_cast<Instruction>(V);
      if (!I) {
        Addends.emplace_back(V, IsPositive);
        continue;
      }

      // If an instruction has more than one user, it indicates that it either
      // has an external user, which will be later checked by the checkNodes
      // function, or it is a subexpression utilized by multiple expressions. In
      // the latter case, we will attempt to separately identify the complex
      // operation from here in order to create a shared
      // ComplexDeinterleavingCompositeNode.
      if (I != Insn && I->getNumUses() > 1) {
        LLVM_DEBUG(dbgs() << "Found potential sub-expression: " << *I << "\n");
        Addends.emplace_back(I, IsPositive);
        continue;
      }
      switch (I->getOpcode()) {
      case Instruction::FAdd:
      case Instruction::Add:
        Worklist.emplace_back(I->getOperand(1), IsPositive);
        Worklist.emplace_back(I->getOperand(0), IsPositive);
        break;
      case Instruction::FSub:
        Worklist.emplace_back(I->getOperand(1), !IsPositive);
        Worklist.emplace_back(I->getOperand(0), IsPositive);
        break;
      case Instruction::Sub:
        if (isNeg(I)) {
          Worklist.emplace_back(getNegOperand(I), !IsPositive);
        } else {
          Worklist.emplace_back(I->getOperand(1), !IsPositive);
          Worklist.emplace_back(I->getOperand(0), IsPositive);
        }
        break;
      case Instruction::FMul:
      case Instruction::Mul: {
        Value *A, *B;
        if (isNeg(I->getOperand(0))) {
          A = getNegOperand(I->getOperand(0));
          IsPositive = !IsPositive;
        } else {
          A = I->getOperand(0);
        }

        if (isNeg(I->getOperand(1))) {
          B = getNegOperand(I->getOperand(1));
          IsPositive = !IsPositive;
        } else {
          B = I->getOperand(1);
        }
        Muls.push_back(Product{A, B, IsPositive});
        break;
      }
      case Instruction::FNeg:
        Worklist.emplace_back(I->getOperand(0), !IsPositive);
        break;
      default:
        Addends.emplace_back(I, IsPositive);
        continue;
      }

      if (Flags && I->getFastMathFlags() != *Flags) {
        LLVM_DEBUG(dbgs() << "The instruction's fast math flags are "
                             "inconsistent with the root instructions' flags: "
                          << *I << "\n");
        return false;
      }
    }
    return true;
  };

  std::vector<Product> RealMuls, ImagMuls;
  std::list<Addend> RealAddends, ImagAddends;
  if (!Collect(Real, RealMuls, RealAddends) ||
      !Collect(Imag, ImagMuls, ImagAddends))
    return nullptr;

  if (RealAddends.size() != ImagAddends.size())
    return nullptr;

  NodePtr FinalNode;
  if (!RealMuls.empty() || !ImagMuls.empty()) {
    // If there are multiplicands, extract positive addend and use it as an
    // accumulator
    FinalNode = extractPositiveAddend(RealAddends, ImagAddends);
    FinalNode = identifyMultiplications(RealMuls, ImagMuls, FinalNode);
    if (!FinalNode)
      return nullptr;
  }

  // Identify and process remaining additions
  if (!RealAddends.empty() || !ImagAddends.empty()) {
    FinalNode = identifyAdditions(RealAddends, ImagAddends, Flags, FinalNode);
    if (!FinalNode)
      return nullptr;
  }
  assert(FinalNode && "FinalNode can not be nullptr here");
  // Set the Real and Imag fields of the final node and submit it
  FinalNode->Real = Real;
  FinalNode->Imag = Imag;
  submitCompositeNode(FinalNode);
  return FinalNode;
}

bool ComplexDeinterleavingGraph::collectPartialMuls(
    const std::vector<Product> &RealMuls, const std::vector<Product> &ImagMuls,
    std::vector<PartialMulCandidate> &PartialMulCandidates) {
  // Helper function to extract a common operand from two products
  auto FindCommonInstruction = [](const Product &Real,
                                  const Product &Imag) -> Value * {
    if (Real.Multiplicand == Imag.Multiplicand ||
        Real.Multiplicand == Imag.Multiplier)
      return Real.Multiplicand;

    if (Real.Multiplier == Imag.Multiplicand ||
        Real.Multiplier == Imag.Multiplier)
      return Real.Multiplier;

    return nullptr;
  };

  // Iterating over real and imaginary multiplications to find common operands
  // If a common operand is found, a partial multiplication candidate is created
  // and added to the candidates vector The function returns false if no common
  // operands are found for any product
  for (unsigned i = 0; i < RealMuls.size(); ++i) {
    bool FoundCommon = false;
    for (unsigned j = 0; j < ImagMuls.size(); ++j) {
      auto *Common = FindCommonInstruction(RealMuls[i], ImagMuls[j]);
      if (!Common)
        continue;

      auto *A = RealMuls[i].Multiplicand == Common ? RealMuls[i].Multiplier
                                                   : RealMuls[i].Multiplicand;
      auto *B = ImagMuls[j].Multiplicand == Common ? ImagMuls[j].Multiplier
                                                   : ImagMuls[j].Multiplicand;

      auto Node = identifyNode(A, B);
      if (Node) {
        FoundCommon = true;
        PartialMulCandidates.push_back({Common, Node, i, j, false});
      }

      Node = identifyNode(B, A);
      if (Node) {
        FoundCommon = true;
        PartialMulCandidates.push_back({Common, Node, i, j, true});
      }
    }
    if (!FoundCommon)
      return false;
  }
  return true;
}

ComplexDeinterleavingGraph::NodePtr
ComplexDeinterleavingGraph::identifyMultiplications(
    std::vector<Product> &RealMuls, std::vector<Product> &ImagMuls,
    NodePtr Accumulator = nullptr) {
  if (RealMuls.size() != ImagMuls.size())
    return nullptr;

  std::vector<PartialMulCandidate> Info;
  if (!collectPartialMuls(RealMuls, ImagMuls, Info))
    return nullptr;

  // Map to store common instruction to node pointers
  std::map<Value *, NodePtr> CommonToNode;
  std::vector<bool> Processed(Info.size(), false);
  for (unsigned I = 0; I < Info.size(); ++I) {
    if (Processed[I])
      continue;

    PartialMulCandidate &InfoA = Info[I];
    for (unsigned J = I + 1; J < Info.size(); ++J) {
      if (Processed[J])
        continue;

      PartialMulCandidate &InfoB = Info[J];
      auto *InfoReal = &InfoA;
      auto *InfoImag = &InfoB;

      auto NodeFromCommon = identifyNode(InfoReal->Common, InfoImag->Common);
      if (!NodeFromCommon) {
        std::swap(InfoReal, InfoImag);
        NodeFromCommon = identifyNode(InfoReal->Common, InfoImag->Common);
      }
      if (!NodeFromCommon)
        continue;

      CommonToNode[InfoReal->Common] = NodeFromCommon;
      CommonToNode[InfoImag->Common] = NodeFromCommon;
      Processed[I] = true;
      Processed[J] = true;
    }
  }

  std::vector<bool> ProcessedReal(RealMuls.size(), false);
  std::vector<bool> ProcessedImag(ImagMuls.size(), false);
  NodePtr Result = Accumulator;
  for (auto &PMI : Info) {
    if (ProcessedReal[PMI.RealIdx] || ProcessedImag[PMI.ImagIdx])
      continue;

    auto It = CommonToNode.find(PMI.Common);
    // TODO: Process independent complex multiplications. Cases like this:
    //  A.real() * B where both A and B are complex numbers.
    if (It == CommonToNode.end()) {
      LLVM_DEBUG({
        dbgs() << "Unprocessed independent partial multiplication:\n";
        for (auto *Mul : {&RealMuls[PMI.RealIdx], &RealMuls[PMI.RealIdx]})
          dbgs().indent(4) << (Mul->IsPositive ? "+" : "-") << *Mul->Multiplier
                           << " multiplied by " << *Mul->Multiplicand << "\n";
      });
      return nullptr;
    }

    auto &RealMul = RealMuls[PMI.RealIdx];
    auto &ImagMul = ImagMuls[PMI.ImagIdx];

    auto NodeA = It->second;
    auto NodeB = PMI.Node;
    auto IsMultiplicandReal = PMI.Common == NodeA->Real;
    // The following table illustrates the relationship between multiplications
    // and rotations. If we consider the multiplication (X + iY) * (U + iV), we
    // can see:
    //
    // Rotation |   Real |   Imag |
    // ---------+--------+--------+
    //        0 |  x * u |  x * v |
    //       90 | -y * v |  y * u |
    //      180 | -x * u | -x * v |
    //      270 |  y * v | -y * u |
    //
    // Check if the candidate can indeed be represented by partial
    // multiplication
    // TODO: Add support for multiplication by complex one
    if ((IsMultiplicandReal && PMI.IsNodeInverted) ||
        (!IsMultiplicandReal && !PMI.IsNodeInverted))
      continue;

    // Determine the rotation based on the multiplications
    ComplexDeinterleavingRotation Rotation;
    if (IsMultiplicandReal) {
      // Detect 0 and 180 degrees rotation
      if (RealMul.IsPositive && ImagMul.IsPositive)
        Rotation = llvm::ComplexDeinterleavingRotation::Rotation_0;
      else if (!RealMul.IsPositive && !ImagMul.IsPositive)
        Rotation = llvm::ComplexDeinterleavingRotation::Rotation_180;
      else
        continue;

    } else {
      // Detect 90 and 270 degrees rotation
      if (!RealMul.IsPositive && ImagMul.IsPositive)
        Rotation = llvm::ComplexDeinterleavingRotation::Rotation_90;
      else if (RealMul.IsPositive && !ImagMul.IsPositive)
        Rotation = llvm::ComplexDeinterleavingRotation::Rotation_270;
      else
        continue;
    }

    LLVM_DEBUG({
      dbgs() << "Identified partial multiplication (X, Y) * (U, V):\n";
      dbgs().indent(4) << "X: " << *NodeA->Real << "\n";
      dbgs().indent(4) << "Y: " << *NodeA->Imag << "\n";
      dbgs().indent(4) << "U: " << *NodeB->Real << "\n";
      dbgs().indent(4) << "V: " << *NodeB->Imag << "\n";
      dbgs().indent(4) << "Rotation - " << (int)Rotation * 90 << "\n";
    });

    NodePtr NodeMul = prepareCompositeNode(
        ComplexDeinterleavingOperation::CMulPartial, nullptr, nullptr);
    NodeMul->Rotation = Rotation;
    NodeMul->addOperand(NodeA);
    NodeMul->addOperand(NodeB);
    if (Result)
      NodeMul->addOperand(Result);
    submitCompositeNode(NodeMul);
    Result = NodeMul;
    ProcessedReal[PMI.RealIdx] = true;
    ProcessedImag[PMI.ImagIdx] = true;
  }

  // Ensure all products have been processed, if not return nullptr.
  if (!all_of(ProcessedReal, [](bool V) { return V; }) ||
      !all_of(ProcessedImag, [](bool V) { return V; })) {

    // Dump debug information about which partial multiplications are not
    // processed.
    LLVM_DEBUG({
      dbgs() << "Unprocessed products (Real):\n";
      for (size_t i = 0; i < ProcessedReal.size(); ++i) {
        if (!ProcessedReal[i])
          dbgs().indent(4) << (RealMuls[i].IsPositive ? "+" : "-")
                           << *RealMuls[i].Multiplier << " multiplied by "
                           << *RealMuls[i].Multiplicand << "\n";
      }
      dbgs() << "Unprocessed products (Imag):\n";
      for (size_t i = 0; i < ProcessedImag.size(); ++i) {
        if (!ProcessedImag[i])
          dbgs().indent(4) << (ImagMuls[i].IsPositive ? "+" : "-")
                           << *ImagMuls[i].Multiplier << " multiplied by "
                           << *ImagMuls[i].Multiplicand << "\n";
      }
    });
    return nullptr;
  }

  return Result;
}

ComplexDeinterleavingGraph::NodePtr
ComplexDeinterleavingGraph::identifyAdditions(
    std::list<Addend> &RealAddends, std::list<Addend> &ImagAddends,
    std::optional<FastMathFlags> Flags, NodePtr Accumulator = nullptr) {
  if (RealAddends.size() != ImagAddends.size())
    return nullptr;

  NodePtr Result;
  // If we have accumulator use it as first addend
  if (Accumulator)
    Result = Accumulator;
  // Otherwise find an element with both positive real and imaginary parts.
  else
    Result = extractPositiveAddend(RealAddends, ImagAddends);

  if (!Result)
    return nullptr;

  while (!RealAddends.empty()) {
    auto ItR = RealAddends.begin();
    auto [R, IsPositiveR] = *ItR;

    bool FoundImag = false;
    for (auto ItI = ImagAddends.begin(); ItI != ImagAddends.end(); ++ItI) {
      auto [I, IsPositiveI] = *ItI;
      ComplexDeinterleavingRotation Rotation;
      if (IsPositiveR && IsPositiveI)
        Rotation = ComplexDeinterleavingRotation::Rotation_0;
      else if (!IsPositiveR && IsPositiveI)
        Rotation = ComplexDeinterleavingRotation::Rotation_90;
      else if (!IsPositiveR && !IsPositiveI)
        Rotation = ComplexDeinterleavingRotation::Rotation_180;
      else
        Rotation = ComplexDeinterleavingRotation::Rotation_270;

      NodePtr AddNode;
      if (Rotation == ComplexDeinterleavingRotation::Rotation_0 ||
          Rotation == ComplexDeinterleavingRotation::Rotation_180) {
        AddNode = identifyNode(R, I);
      } else {
        AddNode = identifyNode(I, R);
      }
      if (AddNode) {
        LLVM_DEBUG({
          dbgs() << "Identified addition:\n";
          dbgs().indent(4) << "X: " << *R << "\n";
          dbgs().indent(4) << "Y: " << *I << "\n";
          dbgs().indent(4) << "Rotation - " << (int)Rotation * 90 << "\n";
        });

        NodePtr TmpNode;
        if (Rotation == llvm::ComplexDeinterleavingRotation::Rotation_0) {
          TmpNode = prepareCompositeNode(
              ComplexDeinterleavingOperation::Symmetric, nullptr, nullptr);
          if (Flags) {
            TmpNode->Opcode = Instruction::FAdd;
            TmpNode->Flags = *Flags;
          } else {
            TmpNode->Opcode = Instruction::Add;
          }
        } else if (Rotation ==
                   llvm::ComplexDeinterleavingRotation::Rotation_180) {
          TmpNode = prepareCompositeNode(
              ComplexDeinterleavingOperation::Symmetric, nullptr, nullptr);
          if (Flags) {
            TmpNode->Opcode = Instruction::FSub;
            TmpNode->Flags = *Flags;
          } else {
            TmpNode->Opcode = Instruction::Sub;
          }
        } else {
          TmpNode = prepareCompositeNode(ComplexDeinterleavingOperation::CAdd,
                                         nullptr, nullptr);
          TmpNode->Rotation = Rotation;
        }

        TmpNode->addOperand(Result);
        TmpNode->addOperand(AddNode);
        submitCompositeNode(TmpNode);
        Result = TmpNode;
        RealAddends.erase(ItR);
        ImagAddends.erase(ItI);
        FoundImag = true;
        break;
      }
    }
    if (!FoundImag)
      return nullptr;
  }
  return Result;
}

ComplexDeinterleavingGraph::NodePtr
ComplexDeinterleavingGraph::extractPositiveAddend(
    std::list<Addend> &RealAddends, std::list<Addend> &ImagAddends) {
  for (auto ItR = RealAddends.begin(); ItR != RealAddends.end(); ++ItR) {
    for (auto ItI = ImagAddends.begin(); ItI != ImagAddends.end(); ++ItI) {
      auto [R, IsPositiveR] = *ItR;
      auto [I, IsPositiveI] = *ItI;
      if (IsPositiveR && IsPositiveI) {
        auto Result = identifyNode(R, I);
        if (Result) {
          RealAddends.erase(ItR);
          ImagAddends.erase(ItI);
          return Result;
        }
      }
    }
  }
  return nullptr;
}

bool ComplexDeinterleavingGraph::identifyNodes(Instruction *RootI) {
  // This potential root instruction might already have been recognized as
  // reduction. Because RootToNode maps both Real and Imaginary parts to
  // CompositeNode we should choose only one either Real or Imag instruction to
  // use as an anchor for generating complex instruction.
  auto It = RootToNode.find(RootI);
  if (It != RootToNode.end()) {
    auto RootNode = It->second;
    assert(RootNode->Operation ==
           ComplexDeinterleavingOperation::ReductionOperation);
    // Find out which part, Real or Imag, comes later, and only if we come to
    // the latest part, add it to OrderedRoots.
    auto *R = cast<Instruction>(RootNode->Real);
    auto *I = cast<Instruction>(RootNode->Imag);
    auto *ReplacementAnchor = R->comesBefore(I) ? I : R;
    if (ReplacementAnchor != RootI)
      return false;
    OrderedRoots.push_back(RootI);
    return true;
  }

  auto RootNode = identifyRoot(RootI);
  if (!RootNode)
    return false;

  LLVM_DEBUG({
    Function *F = RootI->getFunction();
    BasicBlock *B = RootI->getParent();
    dbgs() << "Complex deinterleaving graph for " << F->getName()
           << "::" << B->getName() << ".\n";
    dump(dbgs());
    dbgs() << "\n";
  });
  RootToNode[RootI] = RootNode;
  OrderedRoots.push_back(RootI);
  return true;
}

bool ComplexDeinterleavingGraph::collectPotentialReductions(BasicBlock *B) {
  bool FoundPotentialReduction = false;

  auto *Br = dyn_cast<BranchInst>(B->getTerminator());
  if (!Br || Br->getNumSuccessors() != 2)
    return false;

  // Identify simple one-block loop
  if (Br->getSuccessor(0) != B && Br->getSuccessor(1) != B)
    return false;

  SmallVector<PHINode *> PHIs;
  for (auto &PHI : B->phis()) {
    if (PHI.getNumIncomingValues() != 2)
      continue;

    if (!PHI.getType()->isVectorTy())
      continue;

    auto *ReductionOp = dyn_cast<Instruction>(PHI.getIncomingValueForBlock(B));
    if (!ReductionOp)
      continue;

    // Check if final instruction is reduced outside of current block
    Instruction *FinalReduction = nullptr;
    auto NumUsers = 0u;
    for (auto *U : ReductionOp->users()) {
      ++NumUsers;
      if (U == &PHI)
        continue;
      FinalReduction = dyn_cast<Instruction>(U);
    }

    if (NumUsers != 2 || !FinalReduction || FinalReduction->getParent() == B ||
        isa<PHINode>(FinalReduction))
      continue;

    ReductionInfo[ReductionOp] = {&PHI, FinalReduction};
    BackEdge = B;
    auto BackEdgeIdx = PHI.getBasicBlockIndex(B);
    auto IncomingIdx = BackEdgeIdx == 0 ? 1 : 0;
    Incoming = PHI.getIncomingBlock(IncomingIdx);
    FoundPotentialReduction = true;

    // If the initial value of PHINode is an Instruction, consider it a leaf
    // value of a complex deinterleaving graph.
    if (auto *InitPHI =
            dyn_cast<Instruction>(PHI.getIncomingValueForBlock(Incoming)))
      FinalInstructions.insert(InitPHI);
  }
  return FoundPotentialReduction;
}

void ComplexDeinterleavingGraph::identifyReductionNodes() {
  SmallVector<bool> Processed(ReductionInfo.size(), false);
  SmallVector<Instruction *> OperationInstruction;
  for (auto &P : ReductionInfo)
    OperationInstruction.push_back(P.first);

  // Identify a complex computation by evaluating two reduction operations that
  // potentially could be involved
  for (size_t i = 0; i < OperationInstruction.size(); ++i) {
    if (Processed[i])
      continue;
    for (size_t j = i + 1; j < OperationInstruction.size(); ++j) {
      if (Processed[j])
        continue;

      auto *Real = OperationInstruction[i];
      auto *Imag = OperationInstruction[j];
      if (Real->getType() != Imag->getType())
        continue;

      RealPHI = ReductionInfo[Real].first;
      ImagPHI = ReductionInfo[Imag].first;
      PHIsFound = false;
      auto Node = identifyNode(Real, Imag);
      if (!Node) {
        std::swap(Real, Imag);
        std::swap(RealPHI, ImagPHI);
        Node = identifyNode(Real, Imag);
      }

      // If a node is identified and reduction PHINode is used in the chain of
      // operations, mark its operation instructions as used to prevent
      // re-identification and attach the node to the real part
      if (Node && PHIsFound) {
        LLVM_DEBUG(dbgs() << "Identified reduction starting from instructions: "
                          << *Real << " / " << *Imag << "\n");
        Processed[i] = true;
        Processed[j] = true;
        auto RootNode = prepareCompositeNode(
            ComplexDeinterleavingOperation::ReductionOperation, Real, Imag);
        RootNode->addOperand(Node);
        RootToNode[Real] = RootNode;
        RootToNode[Imag] = RootNode;
        submitCompositeNode(RootNode);
        break;
      }
    }
  }

  RealPHI = nullptr;
  ImagPHI = nullptr;
}

bool ComplexDeinterleavingGraph::checkNodes() {
  // Collect all instructions from roots to leaves
  SmallPtrSet<Instruction *, 16> AllInstructions;
  SmallVector<Instruction *, 8> Worklist;
  for (auto &Pair : RootToNode)
    Worklist.push_back(Pair.first);

  // Extract all instructions that are used by all XCMLA/XCADD/ADD/SUB/NEG
  // chains
  while (!Worklist.empty()) {
    auto *I = Worklist.back();
    Worklist.pop_back();

    if (!AllInstructions.insert(I).second)
      continue;

    for (Value *Op : I->operands()) {
      if (auto *OpI = dyn_cast<Instruction>(Op)) {
        if (!FinalInstructions.count(I))
          Worklist.emplace_back(OpI);
      }
    }
  }

  // Find instructions that have users outside of chain
  SmallVector<Instruction *, 2> OuterInstructions;
  for (auto *I : AllInstructions) {
    // Skip root nodes
    if (RootToNode.count(I))
      continue;

    for (User *U : I->users()) {
      if (AllInstructions.count(cast<Instruction>(U)))
        continue;

      // Found an instruction that is not used by XCMLA/XCADD chain
      Worklist.emplace_back(I);
      break;
    }
  }

  // If any instructions are found to be used outside, find and remove roots
  // that somehow connect to those instructions.
  SmallPtrSet<Instruction *, 16> Visited;
  while (!Worklist.empty()) {
    auto *I = Worklist.back();
    Worklist.pop_back();
    if (!Visited.insert(I).second)
      continue;

    // Found an impacted root node. Removing it from the nodes to be
    // deinterleaved
    if (RootToNode.count(I)) {
      LLVM_DEBUG(dbgs() << "Instruction " << *I
                        << " could be deinterleaved but its chain of complex "
                           "operations have an outside user\n");
      RootToNode.erase(I);
    }

    if (!AllInstructions.count(I) || FinalInstructions.count(I))
      continue;

    for (User *U : I->users())
      Worklist.emplace_back(cast<Instruction>(U));

    for (Value *Op : I->operands()) {
      if (auto *OpI = dyn_cast<Instruction>(Op))
        Worklist.emplace_back(OpI);
    }
  }
  return !RootToNode.empty();
}

ComplexDeinterleavingGraph::NodePtr
ComplexDeinterleavingGraph::identifyRoot(Instruction *RootI) {
  if (auto *Intrinsic = dyn_cast<IntrinsicInst>(RootI)) {
    if (Intrinsic->getIntrinsicID() != Intrinsic::vector_interleave2)
      return nullptr;

    auto *Real = dyn_cast<Instruction>(Intrinsic->getOperand(0));
    auto *Imag = dyn_cast<Instruction>(Intrinsic->getOperand(1));
    if (!Real || !Imag)
      return nullptr;

    return identifyNode(Real, Imag);
  }

  auto *SVI = dyn_cast<ShuffleVectorInst>(RootI);
  if (!SVI)
    return nullptr;

  // Look for a shufflevector that takes separate vectors of the real and
  // imaginary components and recombines them into a single vector.
  if (!isInterleavingMask(SVI->getShuffleMask()))
    return nullptr;

  Instruction *Real;
  Instruction *Imag;
  if (!match(RootI, m_Shuffle(m_Instruction(Real), m_Instruction(Imag))))
    return nullptr;

  return identifyNode(Real, Imag);
}

ComplexDeinterleavingGraph::NodePtr
ComplexDeinterleavingGraph::identifyDeinterleave(Instruction *Real,
                                                 Instruction *Imag) {
  Instruction *I = nullptr;
  Value *FinalValue = nullptr;
  if (match(Real, m_ExtractValue<0>(m_Instruction(I))) &&
      match(Imag, m_ExtractValue<1>(m_Specific(I))) &&
      match(I, m_Intrinsic<Intrinsic::vector_deinterleave2>(
                   m_Value(FinalValue)))) {
    NodePtr PlaceholderNode = prepareCompositeNode(
        llvm::ComplexDeinterleavingOperation::Deinterleave, Real, Imag);
    PlaceholderNode->ReplacementNode = FinalValue;
    FinalInstructions.insert(Real);
    FinalInstructions.insert(Imag);
    return submitCompositeNode(PlaceholderNode);
  }

  auto *RealShuffle = dyn_cast<ShuffleVectorInst>(Real);
  auto *ImagShuffle = dyn_cast<ShuffleVectorInst>(Imag);
  if (!RealShuffle || !ImagShuffle) {
    if (RealShuffle || ImagShuffle)
      LLVM_DEBUG(dbgs() << " - There's a shuffle where there shouldn't be.\n");
    return nullptr;
  }

  Value *RealOp1 = RealShuffle->getOperand(1);
  if (!isa<UndefValue>(RealOp1) && !isa<ConstantAggregateZero>(RealOp1)) {
    LLVM_DEBUG(dbgs() << " - RealOp1 is not undef or zero.\n");
    return nullptr;
  }
  Value *ImagOp1 = ImagShuffle->getOperand(1);
  if (!isa<UndefValue>(ImagOp1) && !isa<ConstantAggregateZero>(ImagOp1)) {
    LLVM_DEBUG(dbgs() << " - ImagOp1 is not undef or zero.\n");
    return nullptr;
  }

  Value *RealOp0 = RealShuffle->getOperand(0);
  Value *ImagOp0 = ImagShuffle->getOperand(0);

  if (RealOp0 != ImagOp0) {
    LLVM_DEBUG(dbgs() << " - Shuffle operands are not equal.\n");
    return nullptr;
  }

  ArrayRef<int> RealMask = RealShuffle->getShuffleMask();
  ArrayRef<int> ImagMask = ImagShuffle->getShuffleMask();
  if (!isDeinterleavingMask(RealMask) || !isDeinterleavingMask(ImagMask)) {
    LLVM_DEBUG(dbgs() << " - Masks are not deinterleaving.\n");
    return nullptr;
  }

  if (RealMask[0] != 0 || ImagMask[0] != 1) {
    LLVM_DEBUG(dbgs() << " - Masks do not have the correct initial value.\n");
    return nullptr;
  }

  // Type checking, the shuffle type should be a vector type of the same
  // scalar type, but half the size
  auto CheckType = [&](ShuffleVectorInst *Shuffle) {
    Value *Op = Shuffle->getOperand(0);
    auto *ShuffleTy = cast<FixedVectorType>(Shuffle->getType());
    auto *OpTy = cast<FixedVectorType>(Op->getType());

    if (OpTy->getScalarType() != ShuffleTy->getScalarType())
      return false;
    if ((ShuffleTy->getNumElements() * 2) != OpTy->getNumElements())
      return false;

    return true;
  };

  auto CheckDeinterleavingShuffle = [&](ShuffleVectorInst *Shuffle) -> bool {
    if (!CheckType(Shuffle))
      return false;

    ArrayRef<int> Mask = Shuffle->getShuffleMask();
    int Last = *Mask.rbegin();

    Value *Op = Shuffle->getOperand(0);
    auto *OpTy = cast<FixedVectorType>(Op->getType());
    int NumElements = OpTy->getNumElements();

    // Ensure that the deinterleaving shuffle only pulls from the first
    // shuffle operand.
    return Last < NumElements;
  };

  if (RealShuffle->getType() != ImagShuffle->getType()) {
    LLVM_DEBUG(dbgs() << " - Shuffle types aren't equal.\n");
    return nullptr;
  }
  if (!CheckDeinterleavingShuffle(RealShuffle)) {
    LLVM_DEBUG(dbgs() << " - RealShuffle is invalid type.\n");
    return nullptr;
  }
  if (!CheckDeinterleavingShuffle(ImagShuffle)) {
    LLVM_DEBUG(dbgs() << " - ImagShuffle is invalid type.\n");
    return nullptr;
  }

  NodePtr PlaceholderNode =
      prepareCompositeNode(llvm::ComplexDeinterleavingOperation::Deinterleave,
                           RealShuffle, ImagShuffle);
  PlaceholderNode->ReplacementNode = RealShuffle->getOperand(0);
  FinalInstructions.insert(RealShuffle);
  FinalInstructions.insert(ImagShuffle);
  return submitCompositeNode(PlaceholderNode);
}

ComplexDeinterleavingGraph::NodePtr
ComplexDeinterleavingGraph::identifySplat(Value *R, Value *I) {
  auto IsSplat = [](Value *V) -> bool {
    // Fixed-width vector with constants
    if (isa<ConstantDataVector>(V))
      return true;

    VectorType *VTy;
    ArrayRef<int> Mask;
    // Splats are represented differently depending on whether the repeated
    // value is a constant or an Instruction
    if (auto *Const = dyn_cast<ConstantExpr>(V)) {
      if (Const->getOpcode() != Instruction::ShuffleVector)
        return false;
      VTy = cast<VectorType>(Const->getType());
      Mask = Const->getShuffleMask();
    } else if (auto *Shuf = dyn_cast<ShuffleVectorInst>(V)) {
      VTy = Shuf->getType();
      Mask = Shuf->getShuffleMask();
    } else {
      return false;
    }

    // When the data type is <1 x Type>, it's not possible to differentiate
    // between the ComplexDeinterleaving::Deinterleave and
    // ComplexDeinterleaving::Splat operations.
    if (!VTy->isScalableTy() && VTy->getElementCount().getKnownMinValue() == 1)
      return false;

    return all_equal(Mask) && Mask[0] == 0;
  };

  if (!IsSplat(R) || !IsSplat(I))
    return nullptr;

  auto *Real = dyn_cast<Instruction>(R);
  auto *Imag = dyn_cast<Instruction>(I);
  if ((!Real && Imag) || (Real && !Imag))
    return nullptr;

  if (Real && Imag) {
    // Non-constant splats should be in the same basic block
    if (Real->getParent() != Imag->getParent())
      return nullptr;

    FinalInstructions.insert(Real);
    FinalInstructions.insert(Imag);
  }
  NodePtr PlaceholderNode =
      prepareCompositeNode(ComplexDeinterleavingOperation::Splat, R, I);
  return submitCompositeNode(PlaceholderNode);
}

ComplexDeinterleavingGraph::NodePtr
ComplexDeinterleavingGraph::identifyPHINode(Instruction *Real,
                                            Instruction *Imag) {
  if (Real != RealPHI || Imag != ImagPHI)
    return nullptr;

  PHIsFound = true;
  NodePtr PlaceholderNode = prepareCompositeNode(
      ComplexDeinterleavingOperation::ReductionPHI, Real, Imag);
  return submitCompositeNode(PlaceholderNode);
}

ComplexDeinterleavingGraph::NodePtr
ComplexDeinterleavingGraph::identifySelectNode(Instruction *Real,
                                               Instruction *Imag) {
  auto *SelectReal = dyn_cast<SelectInst>(Real);
  auto *SelectImag = dyn_cast<SelectInst>(Imag);
  if (!SelectReal || !SelectImag)
    return nullptr;

  Instruction *MaskA, *MaskB;
  Instruction *AR, *AI, *RA, *BI;
  if (!match(Real, m_Select(m_Instruction(MaskA), m_Instruction(AR),
                            m_Instruction(RA))) ||
      !match(Imag, m_Select(m_Instruction(MaskB), m_Instruction(AI),
                            m_Instruction(BI))))
    return nullptr;

  if (MaskA != MaskB && !MaskA->isIdenticalTo(MaskB))
    return nullptr;

  if (!MaskA->getType()->isVectorTy())
    return nullptr;

  auto NodeA = identifyNode(AR, AI);
  if (!NodeA)
    return nullptr;

  auto NodeB = identifyNode(RA, BI);
  if (!NodeB)
    return nullptr;

  NodePtr PlaceholderNode = prepareCompositeNode(
      ComplexDeinterleavingOperation::ReductionSelect, Real, Imag);
  PlaceholderNode->addOperand(NodeA);
  PlaceholderNode->addOperand(NodeB);
  FinalInstructions.insert(MaskA);
  FinalInstructions.insert(MaskB);
  return submitCompositeNode(PlaceholderNode);
}

static Value *replaceSymmetricNode(IRBuilderBase &B, unsigned Opcode,
                                   std::optional<FastMathFlags> Flags,
                                   Value *InputA, Value *InputB) {
  Value *I;
  switch (Opcode) {
  case Instruction::FNeg:
    I = B.CreateFNeg(InputA);
    break;
  case Instruction::FAdd:
    I = B.CreateFAdd(InputA, InputB);
    break;
  case Instruction::Add:
    I = B.CreateAdd(InputA, InputB);
    break;
  case Instruction::FSub:
    I = B.CreateFSub(InputA, InputB);
    break;
  case Instruction::Sub:
    I = B.CreateSub(InputA, InputB);
    break;
  case Instruction::FMul:
    I = B.CreateFMul(InputA, InputB);
    break;
  case Instruction::Mul:
    I = B.CreateMul(InputA, InputB);
    break;
  default:
    llvm_unreachable("Incorrect symmetric opcode");
  }
  if (Flags)
    cast<Instruction>(I)->setFastMathFlags(*Flags);
  return I;
}

Value *ComplexDeinterleavingGraph::replaceNode(IRBuilderBase &Builder,
                                               RawNodePtr Node) {
  if (Node->ReplacementNode)
    return Node->ReplacementNode;

  auto ReplaceOperandIfExist = [&](RawNodePtr &Node, unsigned Idx) -> Value * {
    return Node->Operands.size() > Idx
               ? replaceNode(Builder, Node->Operands[Idx])
               : nullptr;
  };

  Value *ReplacementNode;
  switch (Node->Operation) {
  case ComplexDeinterleavingOperation::CAdd:
  case ComplexDeinterleavingOperation::CMulPartial:
  case ComplexDeinterleavingOperation::Symmetric: {
    Value *Input0 = ReplaceOperandIfExist(Node, 0);
    Value *Input1 = ReplaceOperandIfExist(Node, 1);
    Value *Accumulator = ReplaceOperandIfExist(Node, 2);
    assert(!Input1 || (Input0->getType() == Input1->getType() &&
                       "Node inputs need to be of the same type"));
    assert(!Accumulator ||
           (Input0->getType() == Accumulator->getType() &&
            "Accumulator and input need to be of the same type"));
    if (Node->Operation == ComplexDeinterleavingOperation::Symmetric)
      ReplacementNode = replaceSymmetricNode(Builder, Node->Opcode, Node->Flags,
                                             Input0, Input1);
    else
      ReplacementNode = TL->createComplexDeinterleavingIR(
          Builder, Node->Operation, Node->Rotation, Input0, Input1,
          Accumulator);
    break;
  }
  case ComplexDeinterleavingOperation::Deinterleave:
    llvm_unreachable("Deinterleave node should already have ReplacementNode");
    break;
  case ComplexDeinterleavingOperation::Splat: {
    auto *NewTy = VectorType::getDoubleElementsVectorType(
        cast<VectorType>(Node->Real->getType()));
    auto *R = dyn_cast<Instruction>(Node->Real);
    auto *I = dyn_cast<Instruction>(Node->Imag);
    if (R && I) {
      // Splats that are not constant are interleaved where they are located
      Instruction *InsertPoint = (I->comesBefore(R) ? R : I)->getNextNode();
      IRBuilder<> IRB(InsertPoint);
      ReplacementNode = IRB.CreateIntrinsic(Intrinsic::vector_interleave2,
                                            NewTy, {Node->Real, Node->Imag});
    } else {
      ReplacementNode = Builder.CreateIntrinsic(
          Intrinsic::vector_interleave2, NewTy, {Node->Real, Node->Imag});
    }
    break;
  }
  case ComplexDeinterleavingOperation::ReductionPHI: {
    // If Operation is ReductionPHI, a new empty PHINode is created.
    // It is filled later when the ReductionOperation is processed.
    auto *VTy = cast<VectorType>(Node->Real->getType());
    auto *NewVTy = VectorType::getDoubleElementsVectorType(VTy);
    auto *NewPHI = PHINode::Create(NewVTy, 0, "", BackEdge->getFirstNonPHIIt());
    OldToNewPHI[dyn_cast<PHINode>(Node->Real)] = NewPHI;
    ReplacementNode = NewPHI;
    break;
  }
  case ComplexDeinterleavingOperation::ReductionOperation:
    ReplacementNode = replaceNode(Builder, Node->Operands[0]);
    processReductionOperation(ReplacementNode, Node);
    break;
  case ComplexDeinterleavingOperation::ReductionSelect: {
    auto *MaskReal = cast<Instruction>(Node->Real)->getOperand(0);
    auto *MaskImag = cast<Instruction>(Node->Imag)->getOperand(0);
    auto *A = replaceNode(Builder, Node->Operands[0]);
    auto *B = replaceNode(Builder, Node->Operands[1]);
    auto *NewMaskTy = VectorType::getDoubleElementsVectorType(
        cast<VectorType>(MaskReal->getType()));
    auto *NewMask = Builder.CreateIntrinsic(Intrinsic::vector_interleave2,
                                            NewMaskTy, {MaskReal, MaskImag});
    ReplacementNode = Builder.CreateSelect(NewMask, A, B);
    break;
  }
  }

  assert(ReplacementNode && "Target failed to create Intrinsic call.");
  NumComplexTransformations += 1;
  Node->ReplacementNode = ReplacementNode;
  return ReplacementNode;
}

void ComplexDeinterleavingGraph::processReductionOperation(
    Value *OperationReplacement, RawNodePtr Node) {
  auto *Real = cast<Instruction>(Node->Real);
  auto *Imag = cast<Instruction>(Node->Imag);
  auto *OldPHIReal = ReductionInfo[Real].first;
  auto *OldPHIImag = ReductionInfo[Imag].first;
  auto *NewPHI = OldToNewPHI[OldPHIReal];

  auto *VTy = cast<VectorType>(Real->getType());
  auto *NewVTy = VectorType::getDoubleElementsVectorType(VTy);

  // We have to interleave initial origin values coming from IncomingBlock
  Value *InitReal = OldPHIReal->getIncomingValueForBlock(Incoming);
  Value *InitImag = OldPHIImag->getIncomingValueForBlock(Incoming);

  IRBuilder<> Builder(Incoming->getTerminator());
  auto *NewInit = Builder.CreateIntrinsic(Intrinsic::vector_interleave2, NewVTy,
                                          {InitReal, InitImag});

  NewPHI->addIncoming(NewInit, Incoming);
  NewPHI->addIncoming(OperationReplacement, BackEdge);

  // Deinterleave complex vector outside of loop so that it can be finally
  // reduced
  auto *FinalReductionReal = ReductionInfo[Real].second;
  auto *FinalReductionImag = ReductionInfo[Imag].second;

  Builder.SetInsertPoint(
      &*FinalReductionReal->getParent()->getFirstInsertionPt());
  auto *Deinterleave = Builder.CreateIntrinsic(Intrinsic::vector_deinterleave2,
                                               OperationReplacement->getType(),
                                               OperationReplacement);

  auto *NewReal = Builder.CreateExtractValue(Deinterleave, (uint64_t)0);
  FinalReductionReal->replaceUsesOfWith(Real, NewReal);

  Builder.SetInsertPoint(FinalReductionImag);
  auto *NewImag = Builder.CreateExtractValue(Deinterleave, 1);
  FinalReductionImag->replaceUsesOfWith(Imag, NewImag);
}

void ComplexDeinterleavingGraph::replaceNodes() {
  SmallVector<Instruction *, 16> DeadInstrRoots;
  for (auto *RootInstruction : OrderedRoots) {
    // Check if this potential root went through check process and we can
    // deinterleave it
    if (!RootToNode.count(RootInstruction))
      continue;

    IRBuilder<> Builder(RootInstruction);
    auto RootNode = RootToNode[RootInstruction];
    Value *R = replaceNode(Builder, RootNode.get());

    if (RootNode->Operation ==
        ComplexDeinterleavingOperation::ReductionOperation) {
      auto *RootReal = cast<Instruction>(RootNode->Real);
      auto *RootImag = cast<Instruction>(RootNode->Imag);
      ReductionInfo[RootReal].first->removeIncomingValue(BackEdge);
      ReductionInfo[RootImag].first->removeIncomingValue(BackEdge);
      DeadInstrRoots.push_back(cast<Instruction>(RootReal));
      DeadInstrRoots.push_back(cast<Instruction>(RootImag));
    } else {
      assert(R && "Unable to find replacement for RootInstruction");
      DeadInstrRoots.push_back(RootInstruction);
      RootInstruction->replaceAllUsesWith(R);
    }
  }

  for (auto *I : DeadInstrRoots)
    RecursivelyDeleteTriviallyDeadInstructions(I, TLI);
}
