//===-- Verifier.cpp - Implement the Module Verifier -----------------------==//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file defines the function verifier interface, that can be used for some
// basic correctness checking of input to the system.
//
// Note that this does not provide full `Java style' security and verifications,
// instead it just tries to ensure that code is well-formed.
//
//  * Both of a binary operator's parameters are of the same type
//  * Verify that the indices of mem access instructions match other operands
//  * Verify that arithmetic and other things are only performed on first-class
//    types.  Verify that shifts & logicals only happen on integrals f.e.
//  * All of the constants in a switch statement are of the correct type
//  * The code is in valid SSA form
//  * It should be illegal to put a label into any other type (like a structure)
//    or to return one. [except constant arrays!]
//  * Only phi nodes can be self referential: 'add i32 %0, %0 ; <int>:0' is bad
//  * PHI nodes must have an entry for each predecessor, with no extras.
//  * PHI nodes must be the first thing in a basic block, all grouped together
//  * All basic blocks should only end with terminator insts, not contain them
//  * The entry node to a function must not have predecessors
//  * All Instructions must be embedded into a basic block
//  * Functions cannot take a void-typed parameter
//  * Verify that a function's argument list agrees with it's declared type.
//  * It is illegal to specify a name for a void value.
//  * It is illegal to have a internal global value with no initializer
//  * It is illegal to have a ret instruction that returns a value that does not
//    agree with the function return value type.
//  * Function call argument types match the function prototype
//  * A landing pad is defined by a landingpad instruction, and can be jumped to
//    only by the unwind edge of an invoke instruction.
//  * A landingpad instruction must be the first non-PHI instruction in the
//    block.
//  * Landingpad instructions must be in a function with a personality function.
//  * Convergence control intrinsics are introduced in ConvergentOperations.rst.
//    The applied restrictions are too numerous to list here.
//  * The convergence entry intrinsic and the loop heart must be the first
//    non-PHI instruction in their respective block. This does not conflict with
//    the landing pads, since these two kinds cannot occur in the same block.
//  * All other things that are tested by asserts spread about the code...
//
//===----------------------------------------------------------------------===//

#include "llvm/IR/Verifier.h"
#include "llvm/ADT/APFloat.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/MapVector.h"
#include "llvm/ADT/PostOrderIterator.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallPtrSet.h"
#include "llvm/ADT/SmallSet.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringMap.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/Twine.h"
#include "llvm/BinaryFormat/Dwarf.h"
#include "llvm/IR/Argument.h"
#include "llvm/IR/AttributeMask.h"
#include "llvm/IR/Attributes.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/CFG.h"
#include "llvm/IR/CallingConv.h"
#include "llvm/IR/Comdat.h"
#include "llvm/IR/Constant.h"
#include "llvm/IR/ConstantRange.h"
#include "llvm/IR/ConstantRangeList.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/ConvergenceVerifier.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfo.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/DebugLoc.h"
#include "llvm/IR/DerivedTypes.h"
#include "llvm/IR/Dominators.h"
#include "llvm/IR/EHPersonalities.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/GCStrategy.h"
#include "llvm/IR/GlobalAlias.h"
#include "llvm/IR/GlobalValue.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/InlineAsm.h"
#include "llvm/IR/InstVisitor.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsAArch64.h"
#include "llvm/IR/IntrinsicsAMDGPU.h"
#include "llvm/IR/IntrinsicsARM.h"
#include "llvm/IR/IntrinsicsNVPTX.h"
#include "llvm/IR/IntrinsicsWebAssembly.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/MemoryModelRelaxationAnnotations.h"
#include "llvm/IR/Metadata.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/ModuleSlotTracker.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ProfDataUtils.h"
#include "llvm/IR/Statepoint.h"
#include "llvm/IR/Type.h"
#include "llvm/IR/Use.h"
#include "llvm/IR/User.h"
#include "llvm/IR/VFABIDemangler.h"
#include "llvm/IR/Value.h"
#include "llvm/InitializePasses.h"
#include "llvm/Pass.h"
#include "llvm/Support/AtomicOrdering.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/ModRef.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <cassert>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

using namespace llvm;

static cl::opt<bool> VerifyNoAliasScopeDomination(
    "verify-noalias-scope-decl-dom", cl::Hidden, cl::init(false),
    cl::desc("Ensure that llvm.experimental.noalias.scope.decl for identical "
             "scopes are not dominating"));

namespace llvm {

struct VerifierSupport {
  raw_ostream *OS;
  const Module &M;
  ModuleSlotTracker MST;
  Triple TT;
  const DataLayout &DL;
  LLVMContext &Context;

  /// Track the brokenness of the module while recursively visiting.
  bool Broken = false;
  /// Broken debug info can be "recovered" from by stripping the debug info.
  bool BrokenDebugInfo = false;
  /// Whether to treat broken debug info as an error.
  bool TreatBrokenDebugInfoAsError = true;

  explicit VerifierSupport(raw_ostream *OS, const Module &M)
      : OS(OS), M(M), MST(&M), TT(Triple::normalize(M.getTargetTriple())),
        DL(M.getDataLayout()), Context(M.getContext()) {}

private:
  void Write(const Module *M) {
    *OS << "; ModuleID = '" << M->getModuleIdentifier() << "'\n";
  }

  void Write(const Value *V) {
    if (V)
      Write(*V);
  }

  void Write(const Value &V) {
    if (isa<Instruction>(V)) {
      V.print(*OS, MST);
      *OS << '\n';
    } else {
      V.printAsOperand(*OS, true, MST);
      *OS << '\n';
    }
  }

  void Write(const DbgRecord *DR) {
    if (DR) {
      DR->print(*OS, MST, false);
      *OS << '\n';
    }
  }

  void Write(DbgVariableRecord::LocationType Type) {
    switch (Type) {
    case DbgVariableRecord::LocationType::Value:
      *OS << "value";
      break;
    case DbgVariableRecord::LocationType::Declare:
      *OS << "declare";
      break;
    case DbgVariableRecord::LocationType::Assign:
      *OS << "assign";
      break;
    case DbgVariableRecord::LocationType::End:
      *OS << "end";
      break;
    case DbgVariableRecord::LocationType::Any:
      *OS << "any";
      break;
    };
  }

  void Write(const Metadata *MD) {
    if (!MD)
      return;
    MD->print(*OS, MST, &M);
    *OS << '\n';
  }

  template <class T> void Write(const MDTupleTypedArrayWrapper<T> &MD) {
    Write(MD.get());
  }

  void Write(const NamedMDNode *NMD) {
    if (!NMD)
      return;
    NMD->print(*OS, MST);
    *OS << '\n';
  }

  void Write(Type *T) {
    if (!T)
      return;
    *OS << ' ' << *T;
  }

  void Write(const Comdat *C) {
    if (!C)
      return;
    *OS << *C;
  }

  void Write(const APInt *AI) {
    if (!AI)
      return;
    *OS << *AI << '\n';
  }

  void Write(const unsigned i) { *OS << i << '\n'; }

  // NOLINTNEXTLINE(readability-identifier-naming)
  void Write(const Attribute *A) {
    if (!A)
      return;
    *OS << A->getAsString() << '\n';
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  void Write(const AttributeSet *AS) {
    if (!AS)
      return;
    *OS << AS->getAsString() << '\n';
  }

  // NOLINTNEXTLINE(readability-identifier-naming)
  void Write(const AttributeList *AL) {
    if (!AL)
      return;
    AL->print(*OS);
  }

  void Write(Printable P) { *OS << P << '\n'; }

  template <typename T> void Write(ArrayRef<T> Vs) {
    for (const T &V : Vs)
      Write(V);
  }

  template <typename T1, typename... Ts>
  void WriteTs(const T1 &V1, const Ts &... Vs) {
    Write(V1);
    WriteTs(Vs...);
  }

  template <typename... Ts> void WriteTs() {}

public:
  /// A check failed, so printout out the condition and the message.
  ///
  /// This provides a nice place to put a breakpoint if you want to see why
  /// something is not correct.
  void CheckFailed(const Twine &Message) {
    if (OS)
      *OS << Message << '\n';
    Broken = true;
  }

  /// A check failed (with values to print).
  ///
  /// This calls the Message-only version so that the above is easier to set a
  /// breakpoint on.
  template <typename T1, typename... Ts>
  void CheckFailed(const Twine &Message, const T1 &V1, const Ts &... Vs) {
    CheckFailed(Message);
    if (OS)
      WriteTs(V1, Vs...);
  }

  /// A debug info check failed.
  void DebugInfoCheckFailed(const Twine &Message) {
    if (OS)
      *OS << Message << '\n';
    Broken |= TreatBrokenDebugInfoAsError;
    BrokenDebugInfo = true;
  }

  /// A debug info check failed (with values to print).
  template <typename T1, typename... Ts>
  void DebugInfoCheckFailed(const Twine &Message, const T1 &V1,
                            const Ts &... Vs) {
    DebugInfoCheckFailed(Message);
    if (OS)
      WriteTs(V1, Vs...);
  }
};

} // namespace llvm

namespace {

class Verifier : public InstVisitor<Verifier>, VerifierSupport {
  friend class InstVisitor<Verifier>;
  DominatorTree DT;

  /// When verifying a basic block, keep track of all of the
  /// instructions we have seen so far.
  ///
  /// This allows us to do efficient dominance checks for the case when an
  /// instruction has an operand that is an instruction in the same block.
  SmallPtrSet<Instruction *, 16> InstsInThisBlock;

  /// Keep track of the metadata nodes that have been checked already.
  SmallPtrSet<const Metadata *, 32> MDNodes;

  /// Keep track which DISubprogram is attached to which function.
  DenseMap<const DISubprogram *, const Function *> DISubprogramAttachments;

  /// Track all DICompileUnits visited.
  SmallPtrSet<const Metadata *, 2> CUVisited;

  /// The result type for a landingpad.
  Type *LandingPadResultTy;

  /// Whether we've seen a call to @llvm.localescape in this function
  /// already.
  bool SawFrameEscape;

  /// Whether the current function has a DISubprogram attached to it.
  bool HasDebugInfo = false;

  /// Stores the count of how many objects were passed to llvm.localescape for a
  /// given function and the largest index passed to llvm.localrecover.
  DenseMap<Function *, std::pair<unsigned, unsigned>> FrameEscapeInfo;

  // Maps catchswitches and cleanuppads that unwind to siblings to the
  // terminators that indicate the unwind, used to detect cycles therein.
  MapVector<Instruction *, Instruction *> SiblingFuncletInfo;

  /// Cache which blocks are in which funclet, if an EH funclet personality is
  /// in use. Otherwise empty.
  DenseMap<BasicBlock *, ColorVector> BlockEHFuncletColors;

  /// Cache of constants visited in search of ConstantExprs.
  SmallPtrSet<const Constant *, 32> ConstantExprVisited;

  /// Cache of declarations of the llvm.experimental.deoptimize.<ty> intrinsic.
  SmallVector<const Function *, 4> DeoptimizeDeclarations;

  /// Cache of attribute lists verified.
  SmallPtrSet<const void *, 32> AttributeListsVisited;

  // Verify that this GlobalValue is only used in this module.
  // This map is used to avoid visiting uses twice. We can arrive at a user
  // twice, if they have multiple operands. In particular for very large
  // constant expressions, we can arrive at a particular user many times.
  SmallPtrSet<const Value *, 32> GlobalValueVisited;

  // Keeps track of duplicate function argument debug info.
  SmallVector<const DILocalVariable *, 16> DebugFnArgs;

  TBAAVerifier TBAAVerifyHelper;
  ConvergenceVerifier ConvergenceVerifyHelper;

  SmallVector<IntrinsicInst *, 4> NoAliasScopeDecls;

  void checkAtomicMemAccessSize(Type *Ty, const Instruction *I);

public:
  explicit Verifier(raw_ostream *OS, bool ShouldTreatBrokenDebugInfoAsError,
                    const Module &M)
      : VerifierSupport(OS, M), LandingPadResultTy(nullptr),
        SawFrameEscape(false), TBAAVerifyHelper(this) {
    TreatBrokenDebugInfoAsError = ShouldTreatBrokenDebugInfoAsError;
  }

  bool hasBrokenDebugInfo() const { return BrokenDebugInfo; }

  bool verify(const Function &F) {
    assert(F.getParent() == &M &&
           "An instance of this class only works with a specific module!");

    // First ensure the function is well-enough formed to compute dominance
    // information, and directly compute a dominance tree. We don't rely on the
    // pass manager to provide this as it isolates us from a potentially
    // out-of-date dominator tree and makes it significantly more complex to run
    // this code outside of a pass manager.
    // FIXME: It's really gross that we have to cast away constness here.
    if (!F.empty())
      DT.recalculate(const_cast<Function &>(F));

    for (const BasicBlock &BB : F) {
      if (!BB.empty() && BB.back().isTerminator())
        continue;

      if (OS) {
        *OS << "Basic Block in function '" << F.getName()
            << "' does not have terminator!\n";
        BB.printAsOperand(*OS, true, MST);
        *OS << "\n";
      }
      return false;
    }

    auto FailureCB = [this](const Twine &Message) {
      this->CheckFailed(Message);
    };
    ConvergenceVerifyHelper.initialize(OS, FailureCB, F);

    Broken = false;
    // FIXME: We strip const here because the inst visitor strips const.
    visit(const_cast<Function &>(F));
    verifySiblingFuncletUnwinds();

    if (ConvergenceVerifyHelper.sawTokens())
      ConvergenceVerifyHelper.verify(DT);

    InstsInThisBlock.clear();
    DebugFnArgs.clear();
    LandingPadResultTy = nullptr;
    SawFrameEscape = false;
    SiblingFuncletInfo.clear();
    verifyNoAliasScopeDecl();
    NoAliasScopeDecls.clear();

    return !Broken;
  }

  /// Verify the module that this instance of \c Verifier was initialized with.
  bool verify() {
    Broken = false;

    // Collect all declarations of the llvm.experimental.deoptimize intrinsic.
    for (const Function &F : M)
      if (F.getIntrinsicID() == Intrinsic::experimental_deoptimize)
        DeoptimizeDeclarations.push_back(&F);

    // Now that we've visited every function, verify that we never asked to
    // recover a frame index that wasn't escaped.
    verifyFrameRecoverIndices();
    for (const GlobalVariable &GV : M.globals())
      visitGlobalVariable(GV);

    for (const GlobalAlias &GA : M.aliases())
      visitGlobalAlias(GA);

    for (const GlobalIFunc &GI : M.ifuncs())
      visitGlobalIFunc(GI);

    for (const NamedMDNode &NMD : M.named_metadata())
      visitNamedMDNode(NMD);

    for (const StringMapEntry<Comdat> &SMEC : M.getComdatSymbolTable())
      visitComdat(SMEC.getValue());

    visitModuleFlags();
    visitModuleIdents();
    visitModuleCommandLines();

    verifyCompileUnits();

    verifyDeoptimizeCallingConvs();
    DISubprogramAttachments.clear();
    return !Broken;
  }

private:
  /// Whether a metadata node is allowed to be, or contain, a DILocation.
  enum class AreDebugLocsAllowed { No, Yes };

  // Verification methods...
  void visitGlobalValue(const GlobalValue &GV);
  void visitGlobalVariable(const GlobalVariable &GV);
  void visitGlobalAlias(const GlobalAlias &GA);
  void visitGlobalIFunc(const GlobalIFunc &GI);
  void visitAliaseeSubExpr(const GlobalAlias &A, const Constant &C);
  void visitAliaseeSubExpr(SmallPtrSetImpl<const GlobalAlias *> &Visited,
                           const GlobalAlias &A, const Constant &C);
  void visitNamedMDNode(const NamedMDNode &NMD);
  void visitMDNode(const MDNode &MD, AreDebugLocsAllowed AllowLocs);
  void visitMetadataAsValue(const MetadataAsValue &MD, Function *F);
  void visitValueAsMetadata(const ValueAsMetadata &MD, Function *F);
  void visitDIArgList(const DIArgList &AL, Function *F);
  void visitComdat(const Comdat &C);
  void visitModuleIdents();
  void visitModuleCommandLines();
  void visitModuleFlags();
  void visitModuleFlag(const MDNode *Op,
                       DenseMap<const MDString *, const MDNode *> &SeenIDs,
                       SmallVectorImpl<const MDNode *> &Requirements);
  void visitModuleFlagCGProfileEntry(const MDOperand &MDO);
  void visitFunction(const Function &F);
  void visitBasicBlock(BasicBlock &BB);
  void verifyRangeMetadata(const Value &V, const MDNode *Range, Type *Ty,
                           bool IsAbsoluteSymbol);
  void visitRangeMetadata(Instruction &I, MDNode *Range, Type *Ty);
  void visitDereferenceableMetadata(Instruction &I, MDNode *MD);
  void visitProfMetadata(Instruction &I, MDNode *MD);
  void visitCallStackMetadata(MDNode *MD);
  void visitMemProfMetadata(Instruction &I, MDNode *MD);
  void visitCallsiteMetadata(Instruction &I, MDNode *MD);
  void visitDIAssignIDMetadata(Instruction &I, MDNode *MD);
  void visitMMRAMetadata(Instruction &I, MDNode *MD);
  void visitAnnotationMetadata(MDNode *Annotation);
  void visitAliasScopeMetadata(const MDNode *MD);
  void visitAliasScopeListMetadata(const MDNode *MD);
  void visitAccessGroupMetadata(const MDNode *MD);

  template <class Ty> bool isValidMetadataArray(const MDTuple &N);
#define HANDLE_SPECIALIZED_MDNODE_LEAF(CLASS) void visit##CLASS(const CLASS &N);
#include "llvm/IR/Metadata.def"
  void visitDIScope(const DIScope &N);
  void visitDIVariable(const DIVariable &N);
  void visitDILexicalBlockBase(const DILexicalBlockBase &N);
  void visitDITemplateParameter(const DITemplateParameter &N);

  void visitTemplateParams(const MDNode &N, const Metadata &RawParams);

  void visit(DbgLabelRecord &DLR);
  void visit(DbgVariableRecord &DVR);
  // InstVisitor overrides...
  using InstVisitor<Verifier>::visit;
  void visitDbgRecords(Instruction &I);
  void visit(Instruction &I);

  void visitTruncInst(TruncInst &I);
  void visitZExtInst(ZExtInst &I);
  void visitSExtInst(SExtInst &I);
  void visitFPTruncInst(FPTruncInst &I);
  void visitFPExtInst(FPExtInst &I);
  void visitFPToUIInst(FPToUIInst &I);
  void visitFPToSIInst(FPToSIInst &I);
  void visitUIToFPInst(UIToFPInst &I);
  void visitSIToFPInst(SIToFPInst &I);
  void visitIntToPtrInst(IntToPtrInst &I);
  void visitPtrToIntInst(PtrToIntInst &I);
  void visitBitCastInst(BitCastInst &I);
  void visitAddrSpaceCastInst(AddrSpaceCastInst &I);
  void visitPHINode(PHINode &PN);
  void visitCallBase(CallBase &Call);
  void visitUnaryOperator(UnaryOperator &U);
  void visitBinaryOperator(BinaryOperator &B);
  void visitICmpInst(ICmpInst &IC);
  void visitFCmpInst(FCmpInst &FC);
  void visitExtractElementInst(ExtractElementInst &EI);
  void visitInsertElementInst(InsertElementInst &EI);
  void visitShuffleVectorInst(ShuffleVectorInst &EI);
  void visitVAArgInst(VAArgInst &VAA) { visitInstruction(VAA); }
  void visitCallInst(CallInst &CI);
  void visitInvokeInst(InvokeInst &II);
  void visitGetElementPtrInst(GetElementPtrInst &GEP);
  void visitLoadInst(LoadInst &LI);
  void visitStoreInst(StoreInst &SI);
  void verifyDominatesUse(Instruction &I, unsigned i);
  void visitInstruction(Instruction &I);
  void visitTerminator(Instruction &I);
  void visitBranchInst(BranchInst &BI);
  void visitReturnInst(ReturnInst &RI);
  void visitSwitchInst(SwitchInst &SI);
  void visitIndirectBrInst(IndirectBrInst &BI);
  void visitCallBrInst(CallBrInst &CBI);
  void visitSelectInst(SelectInst &SI);
  void visitUserOp1(Instruction &I);
  void visitUserOp2(Instruction &I) { visitUserOp1(I); }
  void visitIntrinsicCall(Intrinsic::ID ID, CallBase &Call);
  void visitConstrainedFPIntrinsic(ConstrainedFPIntrinsic &FPI);
  void visitVPIntrinsic(VPIntrinsic &VPI);
  void visitDbgIntrinsic(StringRef Kind, DbgVariableIntrinsic &DII);
  void visitDbgLabelIntrinsic(StringRef Kind, DbgLabelInst &DLI);
  void visitAtomicCmpXchgInst(AtomicCmpXchgInst &CXI);
  void visitAtomicRMWInst(AtomicRMWInst &RMWI);
  void visitFenceInst(FenceInst &FI);
  void visitAllocaInst(AllocaInst &AI);
  void visitExtractValueInst(ExtractValueInst &EVI);
  void visitInsertValueInst(InsertValueInst &IVI);
  void visitEHPadPredecessors(Instruction &I);
  void visitLandingPadInst(LandingPadInst &LPI);
  void visitResumeInst(ResumeInst &RI);
  void visitCatchPadInst(CatchPadInst &CPI);
  void visitCatchReturnInst(CatchReturnInst &CatchReturn);
  void visitCleanupPadInst(CleanupPadInst &CPI);
  void visitFuncletPadInst(FuncletPadInst &FPI);
  void visitCatchSwitchInst(CatchSwitchInst &CatchSwitch);
  void visitCleanupReturnInst(CleanupReturnInst &CRI);

  void verifySwiftErrorCall(CallBase &Call, const Value *SwiftErrorVal);
  void verifySwiftErrorValue(const Value *SwiftErrorVal);
  void verifyTailCCMustTailAttrs(const AttrBuilder &Attrs, StringRef Context);
  void verifyMustTailCall(CallInst &CI);
  bool verifyAttributeCount(AttributeList Attrs, unsigned Params);
  void verifyAttributeTypes(AttributeSet Attrs, const Value *V);
  void verifyParameterAttrs(AttributeSet Attrs, Type *Ty, const Value *V);
  void checkUnsignedBaseTenFuncAttr(AttributeList Attrs, StringRef Attr,
                                    const Value *V);
  void verifyFunctionAttrs(FunctionType *FT, AttributeList Attrs,
                           const Value *V, bool IsIntrinsic, bool IsInlineAsm);
  void verifyFunctionMetadata(ArrayRef<std::pair<unsigned, MDNode *>> MDs);

  void visitConstantExprsRecursively(const Constant *EntryC);
  void visitConstantExpr(const ConstantExpr *CE);
  void visitConstantPtrAuth(const ConstantPtrAuth *CPA);
  void verifyInlineAsmCall(const CallBase &Call);
  void verifyStatepoint(const CallBase &Call);
  void verifyFrameRecoverIndices();
  void verifySiblingFuncletUnwinds();

  void verifyFragmentExpression(const DbgVariableIntrinsic &I);
  void verifyFragmentExpression(const DbgVariableRecord &I);
  template <typename ValueOrMetadata>
  void verifyFragmentExpression(const DIVariable &V,
                                DIExpression::FragmentInfo Fragment,
                                ValueOrMetadata *Desc);
  void verifyFnArgs(const DbgVariableIntrinsic &I);
  void verifyFnArgs(const DbgVariableRecord &DVR);
  void verifyNotEntryValue(const DbgVariableIntrinsic &I);
  void verifyNotEntryValue(const DbgVariableRecord &I);

  /// Module-level debug info verification...
  void verifyCompileUnits();

  /// Module-level verification that all @llvm.experimental.deoptimize
  /// declarations share the same calling convention.
  void verifyDeoptimizeCallingConvs();

  void verifyAttachedCallBundle(const CallBase &Call,
                                const OperandBundleUse &BU);

  /// Verify the llvm.experimental.noalias.scope.decl declarations
  void verifyNoAliasScopeDecl();
};

} // end anonymous namespace

/// We know that cond should be true, if not print an error message.
#define Check(C, ...)                                                          \
  do {                                                                         \
    if (!(C)) {                                                                \
      CheckFailed(__VA_ARGS__);                                                \
      return;                                                                  \
    }                                                                          \
  } while (false)

/// We know that a debug info condition should be true, if not print
/// an error message.
#define CheckDI(C, ...)                                                        \
  do {                                                                         \
    if (!(C)) {                                                                \
      DebugInfoCheckFailed(__VA_ARGS__);                                       \
      return;                                                                  \
    }                                                                          \
  } while (false)

void Verifier::visitDbgRecords(Instruction &I) {
  if (!I.DebugMarker)
    return;
  CheckDI(I.DebugMarker->MarkedInstr == &I,
          "Instruction has invalid DebugMarker", &I);
  CheckDI(!isa<PHINode>(&I) || !I.hasDbgRecords(),
          "PHI Node must not have any attached DbgRecords", &I);
  for (DbgRecord &DR : I.getDbgRecordRange()) {
    CheckDI(DR.getMarker() == I.DebugMarker,
            "DbgRecord had invalid DebugMarker", &I, &DR);
    if (auto *Loc =
            dyn_cast_or_null<DILocation>(DR.getDebugLoc().getAsMDNode()))
      visitMDNode(*Loc, AreDebugLocsAllowed::Yes);
    if (auto *DVR = dyn_cast<DbgVariableRecord>(&DR)) {
      visit(*DVR);
      // These have to appear after `visit` for consistency with existing
      // intrinsic behaviour.
      verifyFragmentExpression(*DVR);
      verifyNotEntryValue(*DVR);
    } else if (auto *DLR = dyn_cast<DbgLabelRecord>(&DR)) {
      visit(*DLR);
    }
  }
}

void Verifier::visit(Instruction &I) {
  visitDbgRecords(I);
  for (unsigned i = 0, e = I.getNumOperands(); i != e; ++i)
    Check(I.getOperand(i) != nullptr, "Operand is null", &I);
  InstVisitor<Verifier>::visit(I);
}

// Helper to iterate over indirect users. By returning false, the callback can ask to stop traversing further.
static void forEachUser(const Value *User,
                        SmallPtrSet<const Value *, 32> &Visited,
                        llvm::function_ref<bool(const Value *)> Callback) {
  if (!Visited.insert(User).second)
    return;

  SmallVector<const Value *> WorkList;
  append_range(WorkList, User->materialized_users());
  while (!WorkList.empty()) {
   const Value *Cur = WorkList.pop_back_val();
    if (!Visited.insert(Cur).second)
      continue;
    if (Callback(Cur))
      append_range(WorkList, Cur->materialized_users());
  }
}

void Verifier::visitGlobalValue(const GlobalValue &GV) {
  Check(!GV.isDeclaration() || GV.hasValidDeclarationLinkage(),
        "Global is external, but doesn't have external or weak linkage!", &GV);

  if (const GlobalObject *GO = dyn_cast<GlobalObject>(&GV)) {

    if (MaybeAlign A = GO->getAlign()) {
      Check(A->value() <= Value::MaximumAlignment,
            "huge alignment values are unsupported", GO);
    }

    if (const MDNode *Associated =
            GO->getMetadata(LLVMContext::MD_associated)) {
      Check(Associated->getNumOperands() == 1,
            "associated metadata must have one operand", &GV, Associated);
      const Metadata *Op = Associated->getOperand(0).get();
      Check(Op, "associated metadata must have a global value", GO, Associated);

      const auto *VM = dyn_cast_or_null<ValueAsMetadata>(Op);
      Check(VM, "associated metadata must be ValueAsMetadata", GO, Associated);
      if (VM) {
        Check(isa<PointerType>(VM->getValue()->getType()),
              "associated value must be pointer typed", GV, Associated);

        const Value *Stripped = VM->getValue()->stripPointerCastsAndAliases();
        Check(isa<GlobalObject>(Stripped) || isa<Constant>(Stripped),
              "associated metadata must point to a GlobalObject", GO, Stripped);
        Check(Stripped != GO,
              "global values should not associate to themselves", GO,
              Associated);
      }
    }

    // FIXME: Why is getMetadata on GlobalValue protected?
    if (const MDNode *AbsoluteSymbol =
            GO->getMetadata(LLVMContext::MD_absolute_symbol)) {
      verifyRangeMetadata(*GO, AbsoluteSymbol, DL.getIntPtrType(GO->getType()),
                          true);
    }
  }

  Check(!GV.hasAppendingLinkage() || isa<GlobalVariable>(GV),
        "Only global variables can have appending linkage!", &GV);

  if (GV.hasAppendingLinkage()) {
    const GlobalVariable *GVar = dyn_cast<GlobalVariable>(&GV);
    Check(GVar && GVar->getValueType()->isArrayTy(),
          "Only global arrays can have appending linkage!", GVar);
  }

  if (GV.isDeclarationForLinker())
    Check(!GV.hasComdat(), "Declaration may not be in a Comdat!", &GV);

  if (GV.hasDLLExportStorageClass()) {
    Check(!GV.hasHiddenVisibility(),
          "dllexport GlobalValue must have default or protected visibility",
          &GV);
  }
  if (GV.hasDLLImportStorageClass()) {
    Check(GV.hasDefaultVisibility(),
          "dllimport GlobalValue must have default visibility", &GV);
    Check(!GV.isDSOLocal(), "GlobalValue with DLLImport Storage is dso_local!",
          &GV);

    Check((GV.isDeclaration() &&
           (GV.hasExternalLinkage() || GV.hasExternalWeakLinkage())) ||
              GV.hasAvailableExternallyLinkage(),
          "Global is marked as dllimport, but not external", &GV);
  }

  if (GV.isImplicitDSOLocal())
    Check(GV.isDSOLocal(),
          "GlobalValue with local linkage or non-default "
          "visibility must be dso_local!",
          &GV);

  forEachUser(&GV, GlobalValueVisited, [&](const Value *V) -> bool {
    if (const Instruction *I = dyn_cast<Instruction>(V)) {
      if (!I->getParent() || !I->getParent()->getParent())
        CheckFailed("Global is referenced by parentless instruction!", &GV, &M,
                    I);
      else if (I->getParent()->getParent()->getParent() != &M)
        CheckFailed("Global is referenced in a different module!", &GV, &M, I,
                    I->getParent()->getParent(),
                    I->getParent()->getParent()->getParent());
      return false;
    } else if (const Function *F = dyn_cast<Function>(V)) {
      if (F->getParent() != &M)
        CheckFailed("Global is used by function in a different module", &GV, &M,
                    F, F->getParent());
      return false;
    }
    return true;
  });
}

void Verifier::visitGlobalVariable(const GlobalVariable &GV) {
  if (GV.hasInitializer()) {
    Check(GV.getInitializer()->getType() == GV.getValueType(),
          "Global variable initializer type does not match global "
          "variable type!",
          &GV);
    // If the global has common linkage, it must have a zero initializer and
    // cannot be constant.
    if (GV.hasCommonLinkage()) {
      Check(GV.getInitializer()->isNullValue(),
            "'common' global must have a zero initializer!", &GV);
      Check(!GV.isConstant(), "'common' global may not be marked constant!",
            &GV);
      Check(!GV.hasComdat(), "'common' global may not be in a Comdat!", &GV);
    }
  }

  if (GV.hasName() && (GV.getName() == "llvm.global_ctors" ||
                       GV.getName() == "llvm.global_dtors")) {
    Check(!GV.hasInitializer() || GV.hasAppendingLinkage(),
          "invalid linkage for intrinsic global variable", &GV);
    Check(GV.materialized_use_empty(),
          "invalid uses of intrinsic global variable", &GV);

    // Don't worry about emitting an error for it not being an array,
    // visitGlobalValue will complain on appending non-array.
    if (ArrayType *ATy = dyn_cast<ArrayType>(GV.getValueType())) {
      StructType *STy = dyn_cast<StructType>(ATy->getElementType());
      PointerType *FuncPtrTy =
          PointerType::get(Context, DL.getProgramAddressSpace());
      Check(STy && (STy->getNumElements() == 2 || STy->getNumElements() == 3) &&
                STy->getTypeAtIndex(0u)->isIntegerTy(32) &&
                STy->getTypeAtIndex(1) == FuncPtrTy,
            "wrong type for intrinsic global variable", &GV);
      Check(STy->getNumElements() == 3,
            "the third field of the element type is mandatory, "
            "specify ptr null to migrate from the obsoleted 2-field form");
      Type *ETy = STy->getTypeAtIndex(2);
      Check(ETy->isPointerTy(), "wrong type for intrinsic global variable",
            &GV);
    }
  }

  if (GV.hasName() && (GV.getName() == "llvm.used" ||
                       GV.getName() == "llvm.compiler.used")) {
    Check(!GV.hasInitializer() || GV.hasAppendingLinkage(),
          "invalid linkage for intrinsic global variable", &GV);
    Check(GV.materialized_use_empty(),
          "invalid uses of intrinsic global variable", &GV);

    Type *GVType = GV.getValueType();
    if (ArrayType *ATy = dyn_cast<ArrayType>(GVType)) {
      PointerType *PTy = dyn_cast<PointerType>(ATy->getElementType());
      Check(PTy, "wrong type for intrinsic global variable", &GV);
      if (GV.hasInitializer()) {
        const Constant *Init = GV.getInitializer();
        const ConstantArray *InitArray = dyn_cast<ConstantArray>(Init);
        Check(InitArray, "wrong initalizer for intrinsic global variable",
              Init);
        for (Value *Op : InitArray->operands()) {
          Value *V = Op->stripPointerCasts();
          Check(isa<GlobalVariable>(V) || isa<Function>(V) ||
                    isa<GlobalAlias>(V),
                Twine("invalid ") + GV.getName() + " member", V);
          Check(V->hasName(),
                Twine("members of ") + GV.getName() + " must be named", V);
        }
      }
    }
  }

  // Visit any debug info attachments.
  SmallVector<MDNode *, 1> MDs;
  GV.getMetadata(LLVMContext::MD_dbg, MDs);
  for (auto *MD : MDs) {
    if (auto *GVE = dyn_cast<DIGlobalVariableExpression>(MD))
      visitDIGlobalVariableExpression(*GVE);
    else
      CheckDI(false, "!dbg attachment of global variable must be a "
                     "DIGlobalVariableExpression");
  }

  // Scalable vectors cannot be global variables, since we don't know
  // the runtime size.
  Check(!GV.getValueType()->isScalableTy(),
        "Globals cannot contain scalable types", &GV);

  // Check if it's a target extension type that disallows being used as a
  // global.
  if (auto *TTy = dyn_cast<TargetExtType>(GV.getValueType()))
    Check(TTy->hasProperty(TargetExtType::CanBeGlobal),
          "Global @" + GV.getName() + " has illegal target extension type",
          TTy);

  if (!GV.hasInitializer()) {
    visitGlobalValue(GV);
    return;
  }

  // Walk any aggregate initializers looking for bitcasts between address spaces
  visitConstantExprsRecursively(GV.getInitializer());

  visitGlobalValue(GV);
}

void Verifier::visitAliaseeSubExpr(const GlobalAlias &GA, const Constant &C) {
  SmallPtrSet<const GlobalAlias*, 4> Visited;
  Visited.insert(&GA);
  visitAliaseeSubExpr(Visited, GA, C);
}

void Verifier::visitAliaseeSubExpr(SmallPtrSetImpl<const GlobalAlias*> &Visited,
                                   const GlobalAlias &GA, const Constant &C) {
  if (GA.hasAvailableExternallyLinkage()) {
    Check(isa<GlobalValue>(C) &&
              cast<GlobalValue>(C).hasAvailableExternallyLinkage(),
          "available_externally alias must point to available_externally "
          "global value",
          &GA);
  }
  if (const auto *GV = dyn_cast<GlobalValue>(&C)) {
    if (!GA.hasAvailableExternallyLinkage()) {
      Check(!GV->isDeclarationForLinker(), "Alias must point to a definition",
            &GA);
    }

    if (const auto *GA2 = dyn_cast<GlobalAlias>(GV)) {
      Check(Visited.insert(GA2).second, "Aliases cannot form a cycle", &GA);

      Check(!GA2->isInterposable(),
            "Alias cannot point to an interposable alias", &GA);
    } else {
      // Only continue verifying subexpressions of GlobalAliases.
      // Do not recurse into global initializers.
      return;
    }
  }

  if (const auto *CE = dyn_cast<ConstantExpr>(&C))
    visitConstantExprsRecursively(CE);

  for (const Use &U : C.operands()) {
    Value *V = &*U;
    if (const auto *GA2 = dyn_cast<GlobalAlias>(V))
      visitAliaseeSubExpr(Visited, GA, *GA2->getAliasee());
    else if (const auto *C2 = dyn_cast<Constant>(V))
      visitAliaseeSubExpr(Visited, GA, *C2);
  }
}

void Verifier::visitGlobalAlias(const GlobalAlias &GA) {
  Check(GlobalAlias::isValidLinkage(GA.getLinkage()),
        "Alias should have private, internal, linkonce, weak, linkonce_odr, "
        "weak_odr, external, or available_externally linkage!",
        &GA);
  const Constant *Aliasee = GA.getAliasee();
  Check(Aliasee, "Aliasee cannot be NULL!", &GA);
  Check(GA.getType() == Aliasee->getType(),
        "Alias and aliasee types should match!", &GA);

  Check(isa<GlobalValue>(Aliasee) || isa<ConstantExpr>(Aliasee),
        "Aliasee should be either GlobalValue or ConstantExpr", &GA);

  visitAliaseeSubExpr(GA, *Aliasee);

  visitGlobalValue(GA);
}

void Verifier::visitGlobalIFunc(const GlobalIFunc &GI) {
  Check(GlobalIFunc::isValidLinkage(GI.getLinkage()),
        "IFunc should have private, internal, linkonce, weak, linkonce_odr, "
        "weak_odr, or external linkage!",
        &GI);
  // Pierce through ConstantExprs and GlobalAliases and check that the resolver
  // is a Function definition.
  const Function *Resolver = GI.getResolverFunction();
  Check(Resolver, "IFunc must have a Function resolver", &GI);
  Check(!Resolver->isDeclarationForLinker(),
        "IFunc resolver must be a definition", &GI);

  // Check that the immediate resolver operand (prior to any bitcasts) has the
  // correct type.
  const Type *ResolverTy = GI.getResolver()->getType();

  Check(isa<PointerType>(Resolver->getFunctionType()->getReturnType()),
        "IFunc resolver must return a pointer", &GI);

  const Type *ResolverFuncTy =
      GlobalIFunc::getResolverFunctionType(GI.getValueType());
  Check(ResolverTy == ResolverFuncTy->getPointerTo(GI.getAddressSpace()),
        "IFunc resolver has incorrect type", &GI);
}

void Verifier::visitNamedMDNode(const NamedMDNode &NMD) {
  // There used to be various other llvm.dbg.* nodes, but we don't support
  // upgrading them and we want to reserve the namespace for future uses.
  if (NMD.getName().starts_with("llvm.dbg."))
    CheckDI(NMD.getName() == "llvm.dbg.cu",
            "unrecognized named metadata node in the llvm.dbg namespace", &NMD);
  for (const MDNode *MD : NMD.operands()) {
    if (NMD.getName() == "llvm.dbg.cu")
      CheckDI(MD && isa<DICompileUnit>(MD), "invalid compile unit", &NMD, MD);

    if (!MD)
      continue;

    visitMDNode(*MD, AreDebugLocsAllowed::Yes);
  }
}

void Verifier::visitMDNode(const MDNode &MD, AreDebugLocsAllowed AllowLocs) {
  // Only visit each node once.  Metadata can be mutually recursive, so this
  // avoids infinite recursion here, as well as being an optimization.
  if (!MDNodes.insert(&MD).second)
    return;

  Check(&MD.getContext() == &Context,
        "MDNode context does not match Module context!", &MD);

  switch (MD.getMetadataID()) {
  default:
    llvm_unreachable("Invalid MDNode subclass");
  case Metadata::MDTupleKind:
    break;
#define HANDLE_SPECIALIZED_MDNODE_LEAF(CLASS)                                  \
  case Metadata::CLASS##Kind:                                                  \
    visit##CLASS(cast<CLASS>(MD));                                             \
    break;
#include "llvm/IR/Metadata.def"
  }

  for (const Metadata *Op : MD.operands()) {
    if (!Op)
      continue;
    Check(!isa<LocalAsMetadata>(Op), "Invalid operand for global metadata!",
          &MD, Op);
    CheckDI(!isa<DILocation>(Op) || AllowLocs == AreDebugLocsAllowed::Yes,
            "DILocation not allowed within this metadata node", &MD, Op);
    if (auto *N = dyn_cast<MDNode>(Op)) {
      visitMDNode(*N, AllowLocs);
      continue;
    }
    if (auto *V = dyn_cast<ValueAsMetadata>(Op)) {
      visitValueAsMetadata(*V, nullptr);
      continue;
    }
  }

  // Check these last, so we diagnose problems in operands first.
  Check(!MD.isTemporary(), "Expected no forward declarations!", &MD);
  Check(MD.isResolved(), "All nodes should be resolved!", &MD);
}

void Verifier::visitValueAsMetadata(const ValueAsMetadata &MD, Function *F) {
  Check(MD.getValue(), "Expected valid value", &MD);
  Check(!MD.getValue()->getType()->isMetadataTy(),
        "Unexpected metadata round-trip through values", &MD, MD.getValue());

  auto *L = dyn_cast<LocalAsMetadata>(&MD);
  if (!L)
    return;

  Check(F, "function-local metadata used outside a function", L);

  // If this was an instruction, bb, or argument, verify that it is in the
  // function that we expect.
  Function *ActualF = nullptr;
  if (Instruction *I = dyn_cast<Instruction>(L->getValue())) {
    Check(I->getParent(), "function-local metadata not in basic block", L, I);
    ActualF = I->getParent()->getParent();
  } else if (BasicBlock *BB = dyn_cast<BasicBlock>(L->getValue()))
    ActualF = BB->getParent();
  else if (Argument *A = dyn_cast<Argument>(L->getValue()))
    ActualF = A->getParent();
  assert(ActualF && "Unimplemented function local metadata case!");

  Check(ActualF == F, "function-local metadata used in wrong function", L);
}

void Verifier::visitDIArgList(const DIArgList &AL, Function *F) {
  for (const ValueAsMetadata *VAM : AL.getArgs())
    visitValueAsMetadata(*VAM, F);
}

void Verifier::visitMetadataAsValue(const MetadataAsValue &MDV, Function *F) {
  Metadata *MD = MDV.getMetadata();
  if (auto *N = dyn_cast<MDNode>(MD)) {
    visitMDNode(*N, AreDebugLocsAllowed::No);
    return;
  }

  // Only visit each node once.  Metadata can be mutually recursive, so this
  // avoids infinite recursion here, as well as being an optimization.
  if (!MDNodes.insert(MD).second)
    return;

  if (auto *V = dyn_cast<ValueAsMetadata>(MD))
    visitValueAsMetadata(*V, F);

  if (auto *AL = dyn_cast<DIArgList>(MD))
    visitDIArgList(*AL, F);
}

static bool isType(const Metadata *MD) { return !MD || isa<DIType>(MD); }
static bool isScope(const Metadata *MD) { return !MD || isa<DIScope>(MD); }
static bool isDINode(const Metadata *MD) { return !MD || isa<DINode>(MD); }

void Verifier::visitDILocation(const DILocation &N) {
  CheckDI(N.getRawScope() && isa<DILocalScope>(N.getRawScope()),
          "location requires a valid scope", &N, N.getRawScope());
  if (auto *IA = N.getRawInlinedAt())
    CheckDI(isa<DILocation>(IA), "inlined-at should be a location", &N, IA);
  if (auto *SP = dyn_cast<DISubprogram>(N.getRawScope()))
    CheckDI(SP->isDefinition(), "scope points into the type hierarchy", &N);
}

void Verifier::visitGenericDINode(const GenericDINode &N) {
  CheckDI(N.getTag(), "invalid tag", &N);
}

void Verifier::visitDIScope(const DIScope &N) {
  if (auto *F = N.getRawFile())
    CheckDI(isa<DIFile>(F), "invalid file", &N, F);
}

void Verifier::visitDISubrange(const DISubrange &N) {
  CheckDI(N.getTag() == dwarf::DW_TAG_subrange_type, "invalid tag", &N);
  CheckDI(!N.getRawCountNode() || !N.getRawUpperBound(),
          "Subrange can have any one of count or upperBound", &N);
  auto *CBound = N.getRawCountNode();
  CheckDI(!CBound || isa<ConstantAsMetadata>(CBound) ||
              isa<DIVariable>(CBound) || isa<DIExpression>(CBound),
          "Count must be signed constant or DIVariable or DIExpression", &N);
  auto Count = N.getCount();
  CheckDI(!Count || !isa<ConstantInt *>(Count) ||
              cast<ConstantInt *>(Count)->getSExtValue() >= -1,
          "invalid subrange count", &N);
  auto *LBound = N.getRawLowerBound();
  CheckDI(!LBound || isa<ConstantAsMetadata>(LBound) ||
              isa<DIVariable>(LBound) || isa<DIExpression>(LBound),
          "LowerBound must be signed constant or DIVariable or DIExpression",
          &N);
  auto *UBound = N.getRawUpperBound();
  CheckDI(!UBound || isa<ConstantAsMetadata>(UBound) ||
              isa<DIVariable>(UBound) || isa<DIExpression>(UBound),
          "UpperBound must be signed constant or DIVariable or DIExpression",
          &N);
  auto *Stride = N.getRawStride();
  CheckDI(!Stride || isa<ConstantAsMetadata>(Stride) ||
              isa<DIVariable>(Stride) || isa<DIExpression>(Stride),
          "Stride must be signed constant or DIVariable or DIExpression", &N);
}

void Verifier::visitDIGenericSubrange(const DIGenericSubrange &N) {
  CheckDI(N.getTag() == dwarf::DW_TAG_generic_subrange, "invalid tag", &N);
  CheckDI(!N.getRawCountNode() || !N.getRawUpperBound(),
          "GenericSubrange can have any one of count or upperBound", &N);
  auto *CBound = N.getRawCountNode();
  CheckDI(!CBound || isa<DIVariable>(CBound) || isa<DIExpression>(CBound),
          "Count must be signed constant or DIVariable or DIExpression", &N);
  auto *LBound = N.getRawLowerBound();
  CheckDI(LBound, "GenericSubrange must contain lowerBound", &N);
  CheckDI(isa<DIVariable>(LBound) || isa<DIExpression>(LBound),
          "LowerBound must be signed constant or DIVariable or DIExpression",
          &N);
  auto *UBound = N.getRawUpperBound();
  CheckDI(!UBound || isa<DIVariable>(UBound) || isa<DIExpression>(UBound),
          "UpperBound must be signed constant or DIVariable or DIExpression",
          &N);
  auto *Stride = N.getRawStride();
  CheckDI(Stride, "GenericSubrange must contain stride", &N);
  CheckDI(isa<DIVariable>(Stride) || isa<DIExpression>(Stride),
          "Stride must be signed constant or DIVariable or DIExpression", &N);
}

void Verifier::visitDIEnumerator(const DIEnumerator &N) {
  CheckDI(N.getTag() == dwarf::DW_TAG_enumerator, "invalid tag", &N);
}

void Verifier::visitDIBasicType(const DIBasicType &N) {
  CheckDI(N.getTag() == dwarf::DW_TAG_base_type ||
              N.getTag() == dwarf::DW_TAG_unspecified_type ||
              N.getTag() == dwarf::DW_TAG_string_type,
          "invalid tag", &N);
}

void Verifier::visitDIStringType(const DIStringType &N) {
  CheckDI(N.getTag() == dwarf::DW_TAG_string_type, "invalid tag", &N);
  CheckDI(!(N.isBigEndian() && N.isLittleEndian()), "has conflicting flags",
          &N);
}

void Verifier::visitDIDerivedType(const DIDerivedType &N) {
  // Common scope checks.
  visitDIScope(N);

  CheckDI(N.getTag() == dwarf::DW_TAG_typedef ||
              N.getTag() == dwarf::DW_TAG_pointer_type ||
              N.getTag() == dwarf::DW_TAG_ptr_to_member_type ||
              N.getTag() == dwarf::DW_TAG_reference_type ||
              N.getTag() == dwarf::DW_TAG_rvalue_reference_type ||
              N.getTag() == dwarf::DW_TAG_const_type ||
              N.getTag() == dwarf::DW_TAG_immutable_type ||
              N.getTag() == dwarf::DW_TAG_volatile_type ||
              N.getTag() == dwarf::DW_TAG_restrict_type ||
              N.getTag() == dwarf::DW_TAG_atomic_type ||
              N.getTag() == dwarf::DW_TAG_LLVM_ptrauth_type ||
              N.getTag() == dwarf::DW_TAG_member ||
              (N.getTag() == dwarf::DW_TAG_variable && N.isStaticMember()) ||
              N.getTag() == dwarf::DW_TAG_inheritance ||
              N.getTag() == dwarf::DW_TAG_friend ||
              N.getTag() == dwarf::DW_TAG_set_type ||
              N.getTag() == dwarf::DW_TAG_template_alias,
          "invalid tag", &N);
  if (N.getTag() == dwarf::DW_TAG_ptr_to_member_type) {
    CheckDI(isType(N.getRawExtraData()), "invalid pointer to member type", &N,
            N.getRawExtraData());
  }

  if (N.getTag() == dwarf::DW_TAG_set_type) {
    if (auto *T = N.getRawBaseType()) {
      auto *Enum = dyn_cast_or_null<DICompositeType>(T);
      auto *Basic = dyn_cast_or_null<DIBasicType>(T);
      CheckDI(
          (Enum && Enum->getTag() == dwarf::DW_TAG_enumeration_type) ||
              (Basic && (Basic->getEncoding() == dwarf::DW_ATE_unsigned ||
                         Basic->getEncoding() == dwarf::DW_ATE_signed ||
                         Basic->getEncoding() == dwarf::DW_ATE_unsigned_char ||
                         Basic->getEncoding() == dwarf::DW_ATE_signed_char ||
                         Basic->getEncoding() == dwarf::DW_ATE_boolean)),
          "invalid set base type", &N, T);
    }
  }

  CheckDI(isScope(N.getRawScope()), "invalid scope", &N, N.getRawScope());
  CheckDI(isType(N.getRawBaseType()), "invalid base type", &N,
          N.getRawBaseType());

  if (N.getDWARFAddressSpace()) {
    CheckDI(N.getTag() == dwarf::DW_TAG_pointer_type ||
                N.getTag() == dwarf::DW_TAG_reference_type ||
                N.getTag() == dwarf::DW_TAG_rvalue_reference_type,
            "DWARF address space only applies to pointer or reference types",
            &N);
  }
}

/// Detect mutually exclusive flags.
static bool hasConflictingReferenceFlags(unsigned Flags) {
  return ((Flags & DINode::FlagLValueReference) &&
          (Flags & DINode::FlagRValueReference)) ||
         ((Flags & DINode::FlagTypePassByValue) &&
          (Flags & DINode::FlagTypePassByReference));
}

void Verifier::visitTemplateParams(const MDNode &N, const Metadata &RawParams) {
  auto *Params = dyn_cast<MDTuple>(&RawParams);
  CheckDI(Params, "invalid template params", &N, &RawParams);
  for (Metadata *Op : Params->operands()) {
    CheckDI(Op && isa<DITemplateParameter>(Op), "invalid template parameter",
            &N, Params, Op);
  }
}

void Verifier::visitDICompositeType(const DICompositeType &N) {
  // Common scope checks.
  visitDIScope(N);

  CheckDI(N.getTag() == dwarf::DW_TAG_array_type ||
              N.getTag() == dwarf::DW_TAG_structure_type ||
              N.getTag() == dwarf::DW_TAG_union_type ||
              N.getTag() == dwarf::DW_TAG_enumeration_type ||
              N.getTag() == dwarf::DW_TAG_class_type ||
              N.getTag() == dwarf::DW_TAG_variant_part ||
              N.getTag() == dwarf::DW_TAG_namelist,
          "invalid tag", &N);

  CheckDI(isScope(N.getRawScope()), "invalid scope", &N, N.getRawScope());
  CheckDI(isType(N.getRawBaseType()), "invalid base type", &N,
          N.getRawBaseType());

  CheckDI(!N.getRawElements() || isa<MDTuple>(N.getRawElements()),
          "invalid composite elements", &N, N.getRawElements());
  CheckDI(isType(N.getRawVTableHolder()), "invalid vtable holder", &N,
          N.getRawVTableHolder());
  CheckDI(!hasConflictingReferenceFlags(N.getFlags()),
          "invalid reference flags", &N);
  unsigned DIBlockByRefStruct = 1 << 4;
  CheckDI((N.getFlags() & DIBlockByRefStruct) == 0,
          "DIBlockByRefStruct on DICompositeType is no longer supported", &N);

  if (N.isVector()) {
    const DINodeArray Elements = N.getElements();
    CheckDI(Elements.size() == 1 &&
                Elements[0]->getTag() == dwarf::DW_TAG_subrange_type,
            "invalid vector, expected one element of type subrange", &N);
  }

  if (auto *Params = N.getRawTemplateParams())
    visitTemplateParams(N, *Params);

  if (auto *D = N.getRawDiscriminator()) {
    CheckDI(isa<DIDerivedType>(D) && N.getTag() == dwarf::DW_TAG_variant_part,
            "discriminator can only appear on variant part");
  }

  if (N.getRawDataLocation()) {
    CheckDI(N.getTag() == dwarf::DW_TAG_array_type,
            "dataLocation can only appear in array type");
  }

  if (N.getRawAssociated()) {
    CheckDI(N.getTag() == dwarf::DW_TAG_array_type,
            "associated can only appear in array type");
  }

  if (N.getRawAllocated()) {
    CheckDI(N.getTag() == dwarf::DW_TAG_array_type,
            "allocated can only appear in array type");
  }

  if (N.getRawRank()) {
    CheckDI(N.getTag() == dwarf::DW_TAG_array_type,
            "rank can only appear in array type");
  }

  if (N.getTag() == dwarf::DW_TAG_array_type) {
    CheckDI(N.getRawBaseType(), "array types must have a base type", &N);
  }
}

void Verifier::visitDISubroutineType(const DISubroutineType &N) {
  CheckDI(N.getTag() == dwarf::DW_TAG_subroutine_type, "invalid tag", &N);
  if (auto *Types = N.getRawTypeArray()) {
    CheckDI(isa<MDTuple>(Types), "invalid composite elements", &N, Types);
    for (Metadata *Ty : N.getTypeArray()->operands()) {
      CheckDI(isType(Ty), "invalid subroutine type ref", &N, Types, Ty);
    }
  }
  CheckDI(!hasConflictingReferenceFlags(N.getFlags()),
          "invalid reference flags", &N);
}

void Verifier::visitDIFile(const DIFile &N) {
  CheckDI(N.getTag() == dwarf::DW_TAG_file_type, "invalid tag", &N);
  std::optional<DIFile::ChecksumInfo<StringRef>> Checksum = N.getChecksum();
  if (Checksum) {
    CheckDI(Checksum->Kind <= DIFile::ChecksumKind::CSK_Last,
            "invalid checksum kind", &N);
    size_t Size;
    switch (Checksum->Kind) {
    case DIFile::CSK_MD5:
      Size = 32;
      break;
    case DIFile::CSK_SHA1:
      Size = 40;
      break;
    case DIFile::CSK_SHA256:
      Size = 64;
      break;
    }
    CheckDI(Checksum->Value.size() == Size, "invalid checksum length", &N);
    CheckDI(Checksum->Value.find_if_not(llvm::isHexDigit) == StringRef::npos,
            "invalid checksum", &N);
  }
}

void Verifier::visitDICompileUnit(const DICompileUnit &N) {
  CheckDI(N.isDistinct(), "compile units must be distinct", &N);
  CheckDI(N.getTag() == dwarf::DW_TAG_compile_unit, "invalid tag", &N);

  // Don't bother verifying the compilation directory or producer string
  // as those could be empty.
  CheckDI(N.getRawFile() && isa<DIFile>(N.getRawFile()), "invalid file", &N,
          N.getRawFile());
  CheckDI(!N.getFile()->getFilename().empty(), "invalid filename", &N,
          N.getFile());

  CheckDI((N.getEmissionKind() <= DICompileUnit::LastEmissionKind),
          "invalid emission kind", &N);

  if (auto *Array = N.getRawEnumTypes()) {
    CheckDI(isa<MDTuple>(Array), "invalid enum list", &N, Array);
    for (Metadata *Op : N.getEnumTypes()->operands()) {
      auto *Enum = dyn_cast_or_null<DICompositeType>(Op);
      CheckDI(Enum && Enum->getTag() == dwarf::DW_TAG_enumeration_type,
              "invalid enum type", &N, N.getEnumTypes(), Op);
    }
  }
  if (auto *Array = N.getRawRetainedTypes()) {
    CheckDI(isa<MDTuple>(Array), "invalid retained type list", &N, Array);
    for (Metadata *Op : N.getRetainedTypes()->operands()) {
      CheckDI(
          Op && (isa<DIType>(Op) || (isa<DISubprogram>(Op) &&
                                     !cast<DISubprogram>(Op)->isDefinition())),
          "invalid retained type", &N, Op);
    }
  }
  if (auto *Array = N.getRawGlobalVariables()) {
    CheckDI(isa<MDTuple>(Array), "invalid global variable list", &N, Array);
    for (Metadata *Op : N.getGlobalVariables()->operands()) {
      CheckDI(Op && (isa<DIGlobalVariableExpression>(Op)),
              "invalid global variable ref", &N, Op);
    }
  }
  if (auto *Array = N.getRawImportedEntities()) {
    CheckDI(isa<MDTuple>(Array), "invalid imported entity list", &N, Array);
    for (Metadata *Op : N.getImportedEntities()->operands()) {
      CheckDI(Op && isa<DIImportedEntity>(Op), "invalid imported entity ref",
              &N, Op);
    }
  }
  if (auto *Array = N.getRawMacros()) {
    CheckDI(isa<MDTuple>(Array), "invalid macro list", &N, Array);
    for (Metadata *Op : N.getMacros()->operands()) {
      CheckDI(Op && isa<DIMacroNode>(Op), "invalid macro ref", &N, Op);
    }
  }
  CUVisited.insert(&N);
}

void Verifier::visitDISubprogram(const DISubprogram &N) {
  CheckDI(N.getTag() == dwarf::DW_TAG_subprogram, "invalid tag", &N);
  CheckDI(isScope(N.getRawScope()), "invalid scope", &N, N.getRawScope());
  if (auto *F = N.getRawFile())
    CheckDI(isa<DIFile>(F), "invalid file", &N, F);
  else
    CheckDI(N.getLine() == 0, "line specified with no file", &N, N.getLine());
  if (auto *T = N.getRawType())
    CheckDI(isa<DISubroutineType>(T), "invalid subroutine type", &N, T);
  CheckDI(isType(N.getRawContainingType()), "invalid containing type", &N,
          N.getRawContainingType());
  if (auto *Params = N.getRawTemplateParams())
    visitTemplateParams(N, *Params);
  if (auto *S = N.getRawDeclaration())
    CheckDI(isa<DISubprogram>(S) && !cast<DISubprogram>(S)->isDefinition(),
            "invalid subprogram declaration", &N, S);
  if (auto *RawNode = N.getRawRetainedNodes()) {
    auto *Node = dyn_cast<MDTuple>(RawNode);
    CheckDI(Node, "invalid retained nodes list", &N, RawNode);
    for (Metadata *Op : Node->operands()) {
      CheckDI(Op && (isa<DILocalVariable>(Op) || isa<DILabel>(Op) ||
                     isa<DIImportedEntity>(Op)),
              "invalid retained nodes, expected DILocalVariable, DILabel or "
              "DIImportedEntity",
              &N, Node, Op);
    }
  }
  CheckDI(!hasConflictingReferenceFlags(N.getFlags()),
          "invalid reference flags", &N);

  auto *Unit = N.getRawUnit();
  if (N.isDefinition()) {
    // Subprogram definitions (not part of the type hierarchy).
    CheckDI(N.isDistinct(), "subprogram definitions must be distinct", &N);
    CheckDI(Unit, "subprogram definitions must have a compile unit", &N);
    CheckDI(isa<DICompileUnit>(Unit), "invalid unit type", &N, Unit);
    // There's no good way to cross the CU boundary to insert a nested
    // DISubprogram definition in one CU into a type defined in another CU.
    auto *CT = dyn_cast_or_null<DICompositeType>(N.getRawScope());
    if (CT && CT->getRawIdentifier() &&
        M.getContext().isODRUniquingDebugTypes())
      CheckDI(N.getDeclaration(),
              "definition subprograms cannot be nested within DICompositeType "
              "when enabling ODR",
              &N);
  } else {
    // Subprogram declarations (part of the type hierarchy).
    CheckDI(!Unit, "subprogram declarations must not have a compile unit", &N);
    CheckDI(!N.getRawDeclaration(),
            "subprogram declaration must not have a declaration field");
  }

  if (auto *RawThrownTypes = N.getRawThrownTypes()) {
    auto *ThrownTypes = dyn_cast<MDTuple>(RawThrownTypes);
    CheckDI(ThrownTypes, "invalid thrown types list", &N, RawThrownTypes);
    for (Metadata *Op : ThrownTypes->operands())
      CheckDI(Op && isa<DIType>(Op), "invalid thrown type", &N, ThrownTypes,
              Op);
  }

  if (N.areAllCallsDescribed())
    CheckDI(N.isDefinition(),
            "DIFlagAllCallsDescribed must be attached to a definition");
}

void Verifier::visitDILexicalBlockBase(const DILexicalBlockBase &N) {
  CheckDI(N.getTag() == dwarf::DW_TAG_lexical_block, "invalid tag", &N);
  CheckDI(N.getRawScope() && isa<DILocalScope>(N.getRawScope()),
          "invalid local scope", &N, N.getRawScope());
  if (auto *SP = dyn_cast<DISubprogram>(N.getRawScope()))
    CheckDI(SP->isDefinition(), "scope points into the type hierarchy", &N);
}

void Verifier::visitDILexicalBlock(const DILexicalBlock &N) {
  visitDILexicalBlockBase(N);

  CheckDI(N.getLine() || !N.getColumn(),
          "cannot have column info without line info", &N);
}

void Verifier::visitDILexicalBlockFile(const DILexicalBlockFile &N) {
  visitDILexicalBlockBase(N);
}

void Verifier::visitDICommonBlock(const DICommonBlock &N) {
  CheckDI(N.getTag() == dwarf::DW_TAG_common_block, "invalid tag", &N);
  if (auto *S = N.getRawScope())
    CheckDI(isa<DIScope>(S), "invalid scope ref", &N, S);
  if (auto *S = N.getRawDecl())
    CheckDI(isa<DIGlobalVariable>(S), "invalid declaration", &N, S);
}

void Verifier::visitDINamespace(const DINamespace &N) {
  CheckDI(N.getTag() == dwarf::DW_TAG_namespace, "invalid tag", &N);
  if (auto *S = N.getRawScope())
    CheckDI(isa<DIScope>(S), "invalid scope ref", &N, S);
}

void Verifier::visitDIMacro(const DIMacro &N) {
  CheckDI(N.getMacinfoType() == dwarf::DW_MACINFO_define ||
              N.getMacinfoType() == dwarf::DW_MACINFO_undef,
          "invalid macinfo type", &N);
  CheckDI(!N.getName().empty(), "anonymous macro", &N);
  if (!N.getValue().empty()) {
    assert(N.getValue().data()[0] != ' ' && "Macro value has a space prefix");
  }
}

void Verifier::visitDIMacroFile(const DIMacroFile &N) {
  CheckDI(N.getMacinfoType() == dwarf::DW_MACINFO_start_file,
          "invalid macinfo type", &N);
  if (auto *F = N.getRawFile())
    CheckDI(isa<DIFile>(F), "invalid file", &N, F);

  if (auto *Array = N.getRawElements()) {
    CheckDI(isa<MDTuple>(Array), "invalid macro list", &N, Array);
    for (Metadata *Op : N.getElements()->operands()) {
      CheckDI(Op && isa<DIMacroNode>(Op), "invalid macro ref", &N, Op);
    }
  }
}

void Verifier::visitDIModule(const DIModule &N) {
  CheckDI(N.getTag() == dwarf::DW_TAG_module, "invalid tag", &N);
  CheckDI(!N.getName().empty(), "anonymous module", &N);
}

void Verifier::visitDITemplateParameter(const DITemplateParameter &N) {
  CheckDI(isType(N.getRawType()), "invalid type ref", &N, N.getRawType());
}

void Verifier::visitDITemplateTypeParameter(const DITemplateTypeParameter &N) {
  visitDITemplateParameter(N);

  CheckDI(N.getTag() == dwarf::DW_TAG_template_type_parameter, "invalid tag",
          &N);
}

void Verifier::visitDITemplateValueParameter(
    const DITemplateValueParameter &N) {
  visitDITemplateParameter(N);

  CheckDI(N.getTag() == dwarf::DW_TAG_template_value_parameter ||
              N.getTag() == dwarf::DW_TAG_GNU_template_template_param ||
              N.getTag() == dwarf::DW_TAG_GNU_template_parameter_pack,
          "invalid tag", &N);
}

void Verifier::visitDIVariable(const DIVariable &N) {
  if (auto *S = N.getRawScope())
    CheckDI(isa<DIScope>(S), "invalid scope", &N, S);
  if (auto *F = N.getRawFile())
    CheckDI(isa<DIFile>(F), "invalid file", &N, F);
}

void Verifier::visitDIGlobalVariable(const DIGlobalVariable &N) {
  // Checks common to all variables.
  visitDIVariable(N);

  CheckDI(N.getTag() == dwarf::DW_TAG_variable, "invalid tag", &N);
  CheckDI(isType(N.getRawType()), "invalid type ref", &N, N.getRawType());
  // Check only if the global variable is not an extern
  if (N.isDefinition())
    CheckDI(N.getType(), "missing global variable type", &N);
  if (auto *Member = N.getRawStaticDataMemberDeclaration()) {
    CheckDI(isa<DIDerivedType>(Member),
            "invalid static data member declaration", &N, Member);
  }
}

void Verifier::visitDILocalVariable(const DILocalVariable &N) {
  // Checks common to all variables.
  visitDIVariable(N);

  CheckDI(isType(N.getRawType()), "invalid type ref", &N, N.getRawType());
  CheckDI(N.getTag() == dwarf::DW_TAG_variable, "invalid tag", &N);
  CheckDI(N.getRawScope() && isa<DILocalScope>(N.getRawScope()),
          "local variable requires a valid scope", &N, N.getRawScope());
  if (auto Ty = N.getType())
    CheckDI(!isa<DISubroutineType>(Ty), "invalid type", &N, N.getType());
}

void Verifier::visitDIAssignID(const DIAssignID &N) {
  CheckDI(!N.getNumOperands(), "DIAssignID has no arguments", &N);
  CheckDI(N.isDistinct(), "DIAssignID must be distinct", &N);
}

void Verifier::visitDILabel(const DILabel &N) {
  if (auto *S = N.getRawScope())
    CheckDI(isa<DIScope>(S), "invalid scope", &N, S);
  if (auto *F = N.getRawFile())
    CheckDI(isa<DIFile>(F), "invalid file", &N, F);

  CheckDI(N.getTag() == dwarf::DW_TAG_label, "invalid tag", &N);
  CheckDI(N.getRawScope() && isa<DILocalScope>(N.getRawScope()),
          "label requires a valid scope", &N, N.getRawScope());
}

void Verifier::visitDIExpression(const DIExpression &N) {
  CheckDI(N.isValid(), "invalid expression", &N);
}

void Verifier::visitDIGlobalVariableExpression(
    const DIGlobalVariableExpression &GVE) {
  CheckDI(GVE.getVariable(), "missing variable");
  if (auto *Var = GVE.getVariable())
    visitDIGlobalVariable(*Var);
  if (auto *Expr = GVE.getExpression()) {
    visitDIExpression(*Expr);
    if (auto Fragment = Expr->getFragmentInfo())
      verifyFragmentExpression(*GVE.getVariable(), *Fragment, &GVE);
  }
}

void Verifier::visitDIObjCProperty(const DIObjCProperty &N) {
  CheckDI(N.getTag() == dwarf::DW_TAG_APPLE_property, "invalid tag", &N);
  if (auto *T = N.getRawType())
    CheckDI(isType(T), "invalid type ref", &N, T);
  if (auto *F = N.getRawFile())
    CheckDI(isa<DIFile>(F), "invalid file", &N, F);
}

void Verifier::visitDIImportedEntity(const DIImportedEntity &N) {
  CheckDI(N.getTag() == dwarf::DW_TAG_imported_module ||
              N.getTag() == dwarf::DW_TAG_imported_declaration,
          "invalid tag", &N);
  if (auto *S = N.getRawScope())
    CheckDI(isa<DIScope>(S), "invalid scope for imported entity", &N, S);
  CheckDI(isDINode(N.getRawEntity()), "invalid imported entity", &N,
          N.getRawEntity());
}

void Verifier::visitComdat(const Comdat &C) {
  // In COFF the Module is invalid if the GlobalValue has private linkage.
  // Entities with private linkage don't have entries in the symbol table.
  if (TT.isOSBinFormatCOFF())
    if (const GlobalValue *GV = M.getNamedValue(C.getName()))
      Check(!GV->hasPrivateLinkage(), "comdat global value has private linkage",
            GV);
}

void Verifier::visitModuleIdents() {
  const NamedMDNode *Idents = M.getNamedMetadata("llvm.ident");
  if (!Idents)
    return;

  // llvm.ident takes a list of metadata entry. Each entry has only one string.
  // Scan each llvm.ident entry and make sure that this requirement is met.
  for (const MDNode *N : Idents->operands()) {
    Check(N->getNumOperands() == 1,
          "incorrect number of operands in llvm.ident metadata", N);
    Check(dyn_cast_or_null<MDString>(N->getOperand(0)),
          ("invalid value for llvm.ident metadata entry operand"
           "(the operand should be a string)"),
          N->getOperand(0));
  }
}

void Verifier::visitModuleCommandLines() {
  const NamedMDNode *CommandLines = M.getNamedMetadata("llvm.commandline");
  if (!CommandLines)
    return;

  // llvm.commandline takes a list of metadata entry. Each entry has only one
  // string. Scan each llvm.commandline entry and make sure that this
  // requirement is met.
  for (const MDNode *N : CommandLines->operands()) {
    Check(N->getNumOperands() == 1,
          "incorrect number of operands in llvm.commandline metadata", N);
    Check(dyn_cast_or_null<MDString>(N->getOperand(0)),
          ("invalid value for llvm.commandline metadata entry operand"
           "(the operand should be a string)"),
          N->getOperand(0));
  }
}

void Verifier::visitModuleFlags() {
  const NamedMDNode *Flags = M.getModuleFlagsMetadata();
  if (!Flags) return;

  // Scan each flag, and track the flags and requirements.
  DenseMap<const MDString*, const MDNode*> SeenIDs;
  SmallVector<const MDNode*, 16> Requirements;
  uint64_t PAuthABIPlatform = -1;
  uint64_t PAuthABIVersion = -1;
  for (const MDNode *MDN : Flags->operands()) {
    visitModuleFlag(MDN, SeenIDs, Requirements);
    if (MDN->getNumOperands() != 3)
      continue;
    if (const auto *FlagName = dyn_cast_or_null<MDString>(MDN->getOperand(1))) {
      if (FlagName->getString() == "aarch64-elf-pauthabi-platform") {
        if (const auto *PAP =
                mdconst::dyn_extract_or_null<ConstantInt>(MDN->getOperand(2)))
          PAuthABIPlatform = PAP->getZExtValue();
      } else if (FlagName->getString() == "aarch64-elf-pauthabi-version") {
        if (const auto *PAV =
                mdconst::dyn_extract_or_null<ConstantInt>(MDN->getOperand(2)))
          PAuthABIVersion = PAV->getZExtValue();
      }
    }
  }

  if ((PAuthABIPlatform == uint64_t(-1)) != (PAuthABIVersion == uint64_t(-1)))
    CheckFailed("either both or no 'aarch64-elf-pauthabi-platform' and "
                "'aarch64-elf-pauthabi-version' module flags must be present");

  // Validate that the requirements in the module are valid.
  for (const MDNode *Requirement : Requirements) {
    const MDString *Flag = cast<MDString>(Requirement->getOperand(0));
    const Metadata *ReqValue = Requirement->getOperand(1);

    const MDNode *Op = SeenIDs.lookup(Flag);
    if (!Op) {
      CheckFailed("invalid requirement on flag, flag is not present in module",
                  Flag);
      continue;
    }

    if (Op->getOperand(2) != ReqValue) {
      CheckFailed(("invalid requirement on flag, "
                   "flag does not have the required value"),
                  Flag);
      continue;
    }
  }
}

void
Verifier::visitModuleFlag(const MDNode *Op,
                          DenseMap<const MDString *, const MDNode *> &SeenIDs,
                          SmallVectorImpl<const MDNode *> &Requirements) {
  // Each module flag should have three arguments, the merge behavior (a
  // constant int), the flag ID (an MDString), and the value.
  Check(Op->getNumOperands() == 3,
        "incorrect number of operands in module flag", Op);
  Module::ModFlagBehavior MFB;
  if (!Module::isValidModFlagBehavior(Op->getOperand(0), MFB)) {
    Check(mdconst::dyn_extract_or_null<ConstantInt>(Op->getOperand(0)),
          "invalid behavior operand in module flag (expected constant integer)",
          Op->getOperand(0));
    Check(false,
          "invalid behavior operand in module flag (unexpected constant)",
          Op->getOperand(0));
  }
  MDString *ID = dyn_cast_or_null<MDString>(Op->getOperand(1));
  Check(ID, "invalid ID operand in module flag (expected metadata string)",
        Op->getOperand(1));

  // Check the values for behaviors with additional requirements.
  switch (MFB) {
  case Module::Error:
  case Module::Warning:
  case Module::Override:
    // These behavior types accept any value.
    break;

  case Module::Min: {
    auto *V = mdconst::dyn_extract_or_null<ConstantInt>(Op->getOperand(2));
    Check(V && V->getValue().isNonNegative(),
          "invalid value for 'min' module flag (expected constant non-negative "
          "integer)",
          Op->getOperand(2));
    break;
  }

  case Module::Max: {
    Check(mdconst::dyn_extract_or_null<ConstantInt>(Op->getOperand(2)),
          "invalid value for 'max' module flag (expected constant integer)",
          Op->getOperand(2));
    break;
  }

  case Module::Require: {
    // The value should itself be an MDNode with two operands, a flag ID (an
    // MDString), and a value.
    MDNode *Value = dyn_cast<MDNode>(Op->getOperand(2));
    Check(Value && Value->getNumOperands() == 2,
          "invalid value for 'require' module flag (expected metadata pair)",
          Op->getOperand(2));
    Check(isa<MDString>(Value->getOperand(0)),
          ("invalid value for 'require' module flag "
           "(first value operand should be a string)"),
          Value->getOperand(0));

    // Append it to the list of requirements, to check once all module flags are
    // scanned.
    Requirements.push_back(Value);
    break;
  }

  case Module::Append:
  case Module::AppendUnique: {
    // These behavior types require the operand be an MDNode.
    Check(isa<MDNode>(Op->getOperand(2)),
          "invalid value for 'append'-type module flag "
          "(expected a metadata node)",
          Op->getOperand(2));
    break;
  }
  }

  // Unless this is a "requires" flag, check the ID is unique.
  if (MFB != Module::Require) {
    bool Inserted = SeenIDs.insert(std::make_pair(ID, Op)).second;
    Check(Inserted,
          "module flag identifiers must be unique (or of 'require' type)", ID);
  }

  if (ID->getString() == "wchar_size") {
    ConstantInt *Value
      = mdconst::dyn_extract_or_null<ConstantInt>(Op->getOperand(2));
    Check(Value, "wchar_size metadata requires constant integer argument");
  }

  if (ID->getString() == "Linker Options") {
    // If the llvm.linker.options named metadata exists, we assume that the
    // bitcode reader has upgraded the module flag. Otherwise the flag might
    // have been created by a client directly.
    Check(M.getNamedMetadata("llvm.linker.options"),
          "'Linker Options' named metadata no longer supported");
  }

  if (ID->getString() == "SemanticInterposition") {
    ConstantInt *Value =
        mdconst::dyn_extract_or_null<ConstantInt>(Op->getOperand(2));
    Check(Value,
          "SemanticInterposition metadata requires constant integer argument");
  }

  if (ID->getString() == "CG Profile") {
    for (const MDOperand &MDO : cast<MDNode>(Op->getOperand(2))->operands())
      visitModuleFlagCGProfileEntry(MDO);
  }
}

void Verifier::visitModuleFlagCGProfileEntry(const MDOperand &MDO) {
  auto CheckFunction = [&](const MDOperand &FuncMDO) {
    if (!FuncMDO)
      return;
    auto F = dyn_cast<ValueAsMetadata>(FuncMDO);
    Check(F && isa<Function>(F->getValue()->stripPointerCasts()),
          "expected a Function or null", FuncMDO);
  };
  auto Node = dyn_cast_or_null<MDNode>(MDO);
  Check(Node && Node->getNumOperands() == 3, "expected a MDNode triple", MDO);
  CheckFunction(Node->getOperand(0));
  CheckFunction(Node->getOperand(1));
  auto Count = dyn_cast_or_null<ConstantAsMetadata>(Node->getOperand(2));
  Check(Count && Count->getType()->isIntegerTy(),
        "expected an integer constant", Node->getOperand(2));
}

void Verifier::verifyAttributeTypes(AttributeSet Attrs, const Value *V) {
  for (Attribute A : Attrs) {

    if (A.isStringAttribute()) {
#define GET_ATTR_NAMES
#define ATTRIBUTE_ENUM(ENUM_NAME, DISPLAY_NAME)
#define ATTRIBUTE_STRBOOL(ENUM_NAME, DISPLAY_NAME)                             \
  if (A.getKindAsString() == #DISPLAY_NAME) {                                  \
    auto V = A.getValueAsString();                                             \
    if (!(V.empty() || V == "true" || V == "false"))                           \
      CheckFailed("invalid value for '" #DISPLAY_NAME "' attribute: " + V +    \
                  "");                                                         \
  }

#include "llvm/IR/Attributes.inc"
      continue;
    }

    if (A.isIntAttribute() != Attribute::isIntAttrKind(A.getKindAsEnum())) {
      CheckFailed("Attribute '" + A.getAsString() + "' should have an Argument",
                  V);
      return;
    }
  }
}

// VerifyParameterAttrs - Check the given attributes for an argument or return
// value of the specified type.  The value V is printed in error messages.
void Verifier::verifyParameterAttrs(AttributeSet Attrs, Type *Ty,
                                    const Value *V) {
  if (!Attrs.hasAttributes())
    return;

  verifyAttributeTypes(Attrs, V);

  for (Attribute Attr : Attrs)
    Check(Attr.isStringAttribute() ||
              Attribute::canUseAsParamAttr(Attr.getKindAsEnum()),
          "Attribute '" + Attr.getAsString() + "' does not apply to parameters",
          V);

  if (Attrs.hasAttribute(Attribute::ImmArg)) {
    Check(Attrs.getNumAttributes() == 1,
          "Attribute 'immarg' is incompatible with other attributes", V);
  }

  // Check for mutually incompatible attributes.  Only inreg is compatible with
  // sret.
  unsigned AttrCount = 0;
  AttrCount += Attrs.hasAttribute(Attribute::ByVal);
  AttrCount += Attrs.hasAttribute(Attribute::InAlloca);
  AttrCount += Attrs.hasAttribute(Attribute::Preallocated);
  AttrCount += Attrs.hasAttribute(Attribute::StructRet) ||
               Attrs.hasAttribute(Attribute::InReg);
  AttrCount += Attrs.hasAttribute(Attribute::Nest);
  AttrCount += Attrs.hasAttribute(Attribute::ByRef);
  Check(AttrCount <= 1,
        "Attributes 'byval', 'inalloca', 'preallocated', 'inreg', 'nest', "
        "'byref', and 'sret' are incompatible!",
        V);

  Check(!(Attrs.hasAttribute(Attribute::InAlloca) &&
          Attrs.hasAttribute(Attribute::ReadOnly)),
        "Attributes "
        "'inalloca and readonly' are incompatible!",
        V);

  Check(!(Attrs.hasAttribute(Attribute::StructRet) &&
          Attrs.hasAttribute(Attribute::Returned)),
        "Attributes "
        "'sret and returned' are incompatible!",
        V);

  Check(!(Attrs.hasAttribute(Attribute::ZExt) &&
          Attrs.hasAttribute(Attribute::SExt)),
        "Attributes "
        "'zeroext and signext' are incompatible!",
        V);

  Check(!(Attrs.hasAttribute(Attribute::ReadNone) &&
          Attrs.hasAttribute(Attribute::ReadOnly)),
        "Attributes "
        "'readnone and readonly' are incompatible!",
        V);

  Check(!(Attrs.hasAttribute(Attribute::ReadNone) &&
          Attrs.hasAttribute(Attribute::WriteOnly)),
        "Attributes "
        "'readnone and writeonly' are incompatible!",
        V);

  Check(!(Attrs.hasAttribute(Attribute::ReadOnly) &&
          Attrs.hasAttribute(Attribute::WriteOnly)),
        "Attributes "
        "'readonly and writeonly' are incompatible!",
        V);

  Check(!(Attrs.hasAttribute(Attribute::NoInline) &&
          Attrs.hasAttribute(Attribute::AlwaysInline)),
        "Attributes "
        "'noinline and alwaysinline' are incompatible!",
        V);

  Check(!(Attrs.hasAttribute(Attribute::Writable) &&
          Attrs.hasAttribute(Attribute::ReadNone)),
        "Attributes writable and readnone are incompatible!", V);

  Check(!(Attrs.hasAttribute(Attribute::Writable) &&
          Attrs.hasAttribute(Attribute::ReadOnly)),
        "Attributes writable and readonly are incompatible!", V);

  AttributeMask IncompatibleAttrs = AttributeFuncs::typeIncompatible(Ty);
  for (Attribute Attr : Attrs) {
    if (!Attr.isStringAttribute() &&
        IncompatibleAttrs.contains(Attr.getKindAsEnum())) {
      CheckFailed("Attribute '" + Attr.getAsString() +
                  "' applied to incompatible type!", V);
      return;
    }
  }

  if (isa<PointerType>(Ty)) {
    if (Attrs.hasAttribute(Attribute::Alignment)) {
      Align AttrAlign = Attrs.getAlignment().valueOrOne();
      Check(AttrAlign.value() <= Value::MaximumAlignment,
            "huge alignment values are unsupported", V);
    }
    if (Attrs.hasAttribute(Attribute::ByVal)) {
      SmallPtrSet<Type *, 4> Visited;
      Check(Attrs.getByValType()->isSized(&Visited),
            "Attribute 'byval' does not support unsized types!", V);
      Check(DL.getTypeAllocSize(Attrs.getByValType()).getKnownMinValue() <
                (1ULL << 32),
            "huge 'byval' arguments are unsupported", V);
    }
    if (Attrs.hasAttribute(Attribute::ByRef)) {
      SmallPtrSet<Type *, 4> Visited;
      Check(Attrs.getByRefType()->isSized(&Visited),
            "Attribute 'byref' does not support unsized types!", V);
      Check(DL.getTypeAllocSize(Attrs.getByRefType()).getKnownMinValue() <
                (1ULL << 32),
            "huge 'byref' arguments are unsupported", V);
    }
    if (Attrs.hasAttribute(Attribute::InAlloca)) {
      SmallPtrSet<Type *, 4> Visited;
      Check(Attrs.getInAllocaType()->isSized(&Visited),
            "Attribute 'inalloca' does not support unsized types!", V);
      Check(DL.getTypeAllocSize(Attrs.getInAllocaType()).getKnownMinValue() <
                (1ULL << 32),
            "huge 'inalloca' arguments are unsupported", V);
    }
    if (Attrs.hasAttribute(Attribute::Preallocated)) {
      SmallPtrSet<Type *, 4> Visited;
      Check(Attrs.getPreallocatedType()->isSized(&Visited),
            "Attribute 'preallocated' does not support unsized types!", V);
      Check(
          DL.getTypeAllocSize(Attrs.getPreallocatedType()).getKnownMinValue() <
              (1ULL << 32),
          "huge 'preallocated' arguments are unsupported", V);
    }
  }

  if (Attrs.hasAttribute(Attribute::Initializes)) {
    auto Inits = Attrs.getAttribute(Attribute::Initializes).getInitializes();
    Check(!Inits.empty(), "Attribute 'initializes' does not support empty list",
          V);
    Check(ConstantRangeList::isOrderedRanges(Inits),
          "Attribute 'initializes' does not support unordered ranges", V);
  }

  if (Attrs.hasAttribute(Attribute::NoFPClass)) {
    uint64_t Val = Attrs.getAttribute(Attribute::NoFPClass).getValueAsInt();
    Check(Val != 0, "Attribute 'nofpclass' must have at least one test bit set",
          V);
    Check((Val & ~static_cast<unsigned>(fcAllFlags)) == 0,
          "Invalid value for 'nofpclass' test mask", V);
  }
  if (Attrs.hasAttribute(Attribute::Range)) {
    const ConstantRange &CR =
        Attrs.getAttribute(Attribute::Range).getValueAsConstantRange();
    Check(Ty->isIntOrIntVectorTy(CR.getBitWidth()),
          "Range bit width must match type bit width!", V);
  }
}

void Verifier::checkUnsignedBaseTenFuncAttr(AttributeList Attrs, StringRef Attr,
                                            const Value *V) {
  if (Attrs.hasFnAttr(Attr)) {
    StringRef S = Attrs.getFnAttr(Attr).getValueAsString();
    unsigned N;
    if (S.getAsInteger(10, N))
      CheckFailed("\"" + Attr + "\" takes an unsigned integer: " + S, V);
  }
}

// Check parameter attributes against a function type.
// The value V is printed in error messages.
void Verifier::verifyFunctionAttrs(FunctionType *FT, AttributeList Attrs,
                                   const Value *V, bool IsIntrinsic,
                                   bool IsInlineAsm) {
  if (Attrs.isEmpty())
    return;

  if (AttributeListsVisited.insert(Attrs.getRawPointer()).second) {
    Check(Attrs.hasParentContext(Context),
          "Attribute list does not match Module context!", &Attrs, V);
    for (const auto &AttrSet : Attrs) {
      Check(!AttrSet.hasAttributes() || AttrSet.hasParentContext(Context),
            "Attribute set does not match Module context!", &AttrSet, V);
      for (const auto &A : AttrSet) {
        Check(A.hasParentContext(Context),
              "Attribute does not match Module context!", &A, V);
      }
    }
  }

  bool SawNest = false;
  bool SawReturned = false;
  bool SawSRet = false;
  bool SawSwiftSelf = false;
  bool SawSwiftAsync = false;
  bool SawSwiftError = false;

  // Verify return value attributes.
  AttributeSet RetAttrs = Attrs.getRetAttrs();
  for (Attribute RetAttr : RetAttrs)
    Check(RetAttr.isStringAttribute() ||
              Attribute::canUseAsRetAttr(RetAttr.getKindAsEnum()),
          "Attribute '" + RetAttr.getAsString() +
              "' does not apply to function return values",
          V);

  unsigned MaxParameterWidth = 0;
  auto GetMaxParameterWidth = [&MaxParameterWidth](Type *Ty) {
    if (Ty->isVectorTy()) {
      if (auto *VT = dyn_cast<FixedVectorType>(Ty)) {
        unsigned Size = VT->getPrimitiveSizeInBits().getFixedValue();
        if (Size > MaxParameterWidth)
          MaxParameterWidth = Size;
      }
    }
  };
  GetMaxParameterWidth(FT->getReturnType());
  verifyParameterAttrs(RetAttrs, FT->getReturnType(), V);

  // Verify parameter attributes.
  for (unsigned i = 0, e = FT->getNumParams(); i != e; ++i) {
    Type *Ty = FT->getParamType(i);
    AttributeSet ArgAttrs = Attrs.getParamAttrs(i);

    if (!IsIntrinsic) {
      Check(!ArgAttrs.hasAttribute(Attribute::ImmArg),
            "immarg attribute only applies to intrinsics", V);
      if (!IsInlineAsm)
        Check(!ArgAttrs.hasAttribute(Attribute::ElementType),
              "Attribute 'elementtype' can only be applied to intrinsics"
              " and inline asm.",
              V);
    }

    verifyParameterAttrs(ArgAttrs, Ty, V);
    GetMaxParameterWidth(Ty);

    if (ArgAttrs.hasAttribute(Attribute::Nest)) {
      Check(!SawNest, "More than one parameter has attribute nest!", V);
      SawNest = true;
    }

    if (ArgAttrs.hasAttribute(Attribute::Returned)) {
      Check(!SawReturned, "More than one parameter has attribute returned!", V);
      Check(Ty->canLosslesslyBitCastTo(FT->getReturnType()),
            "Incompatible argument and return types for 'returned' attribute",
            V);
      SawReturned = true;
    }

    if (ArgAttrs.hasAttribute(Attribute::StructRet)) {
      Check(!SawSRet, "Cannot have multiple 'sret' parameters!", V);
      Check(i == 0 || i == 1,
            "Attribute 'sret' is not on first or second parameter!", V);
      SawSRet = true;
    }

    if (ArgAttrs.hasAttribute(Attribute::SwiftSelf)) {
      Check(!SawSwiftSelf, "Cannot have multiple 'swiftself' parameters!", V);
      SawSwiftSelf = true;
    }

    if (ArgAttrs.hasAttribute(Attribute::SwiftAsync)) {
      Check(!SawSwiftAsync, "Cannot have multiple 'swiftasync' parameters!", V);
      SawSwiftAsync = true;
    }

    if (ArgAttrs.hasAttribute(Attribute::SwiftError)) {
      Check(!SawSwiftError, "Cannot have multiple 'swifterror' parameters!", V);
      SawSwiftError = true;
    }

    if (ArgAttrs.hasAttribute(Attribute::InAlloca)) {
      Check(i == FT->getNumParams() - 1,
            "inalloca isn't on the last parameter!", V);
    }
  }

  if (!Attrs.hasFnAttrs())
    return;

  verifyAttributeTypes(Attrs.getFnAttrs(), V);
  for (Attribute FnAttr : Attrs.getFnAttrs())
    Check(FnAttr.isStringAttribute() ||
              Attribute::canUseAsFnAttr(FnAttr.getKindAsEnum()),
          "Attribute '" + FnAttr.getAsString() +
              "' does not apply to functions!",
          V);

  Check(!(Attrs.hasFnAttr(Attribute::NoInline) &&
          Attrs.hasFnAttr(Attribute::AlwaysInline)),
        "Attributes 'noinline and alwaysinline' are incompatible!", V);

  if (Attrs.hasFnAttr(Attribute::OptimizeNone)) {
    Check(Attrs.hasFnAttr(Attribute::NoInline),
          "Attribute 'optnone' requires 'noinline'!", V);

    Check(!Attrs.hasFnAttr(Attribute::OptimizeForSize),
          "Attributes 'optsize and optnone' are incompatible!", V);

    Check(!Attrs.hasFnAttr(Attribute::MinSize),
          "Attributes 'minsize and optnone' are incompatible!", V);

    Check(!Attrs.hasFnAttr(Attribute::OptimizeForDebugging),
          "Attributes 'optdebug and optnone' are incompatible!", V);
  }

  if (Attrs.hasFnAttr(Attribute::OptimizeForDebugging)) {
    Check(!Attrs.hasFnAttr(Attribute::OptimizeForSize),
          "Attributes 'optsize and optdebug' are incompatible!", V);

    Check(!Attrs.hasFnAttr(Attribute::MinSize),
          "Attributes 'minsize and optdebug' are incompatible!", V);
  }

  Check(!Attrs.hasAttrSomewhere(Attribute::Writable) ||
        isModSet(Attrs.getMemoryEffects().getModRef(IRMemLocation::ArgMem)),
        "Attribute writable and memory without argmem: write are incompatible!",
        V);

  if (Attrs.hasFnAttr("aarch64_pstate_sm_enabled")) {
    Check(!Attrs.hasFnAttr("aarch64_pstate_sm_compatible"),
           "Attributes 'aarch64_pstate_sm_enabled and "
           "aarch64_pstate_sm_compatible' are incompatible!",
           V);
  }

  Check((Attrs.hasFnAttr("aarch64_new_za") + Attrs.hasFnAttr("aarch64_in_za") +
         Attrs.hasFnAttr("aarch64_inout_za") +
         Attrs.hasFnAttr("aarch64_out_za") +
         Attrs.hasFnAttr("aarch64_preserves_za")) <= 1,
        "Attributes 'aarch64_new_za', 'aarch64_in_za', 'aarch64_out_za', "
        "'aarch64_inout_za' and 'aarch64_preserves_za' are mutually exclusive",
        V);

  Check(
      (Attrs.hasFnAttr("aarch64_new_zt0") + Attrs.hasFnAttr("aarch64_in_zt0") +
       Attrs.hasFnAttr("aarch64_inout_zt0") +
       Attrs.hasFnAttr("aarch64_out_zt0") +
       Attrs.hasFnAttr("aarch64_preserves_zt0")) <= 1,
      "Attributes 'aarch64_new_zt0', 'aarch64_in_zt0', 'aarch64_out_zt0', "
      "'aarch64_inout_zt0' and 'aarch64_preserves_zt0' are mutually exclusive",
      V);

  if (Attrs.hasFnAttr(Attribute::JumpTable)) {
    const GlobalValue *GV = cast<GlobalValue>(V);
    Check(GV->hasGlobalUnnamedAddr(),
          "Attribute 'jumptable' requires 'unnamed_addr'", V);
  }

  if (auto Args = Attrs.getFnAttrs().getAllocSizeArgs()) {
    auto CheckParam = [&](StringRef Name, unsigned ParamNo) {
      if (ParamNo >= FT->getNumParams()) {
        CheckFailed("'allocsize' " + Name + " argument is out of bounds", V);
        return false;
      }

      if (!FT->getParamType(ParamNo)->isIntegerTy()) {
        CheckFailed("'allocsize' " + Name +
                        " argument must refer to an integer parameter",
                    V);
        return false;
      }

      return true;
    };

    if (!CheckParam("element size", Args->first))
      return;

    if (Args->second && !CheckParam("number of elements", *Args->second))
      return;
  }

  if (Attrs.hasFnAttr(Attribute::AllocKind)) {
    AllocFnKind K = Attrs.getAllocKind();
    AllocFnKind Type =
        K & (AllocFnKind::Alloc | AllocFnKind::Realloc | AllocFnKind::Free);
    if (!is_contained(
            {AllocFnKind::Alloc, AllocFnKind::Realloc, AllocFnKind::Free},
            Type))
      CheckFailed(
          "'allockind()' requires exactly one of alloc, realloc, and free");
    if ((Type == AllocFnKind::Free) &&
        ((K & (AllocFnKind::Uninitialized | AllocFnKind::Zeroed |
               AllocFnKind::Aligned)) != AllocFnKind::Unknown))
      CheckFailed("'allockind(\"free\")' doesn't allow uninitialized, zeroed, "
                  "or aligned modifiers.");
    AllocFnKind ZeroedUninit = AllocFnKind::Uninitialized | AllocFnKind::Zeroed;
    if ((K & ZeroedUninit) == ZeroedUninit)
      CheckFailed("'allockind()' can't be both zeroed and uninitialized");
  }

  if (Attrs.hasFnAttr(Attribute::VScaleRange)) {
    unsigned VScaleMin = Attrs.getFnAttrs().getVScaleRangeMin();
    if (VScaleMin == 0)
      CheckFailed("'vscale_range' minimum must be greater than 0", V);
    else if (!isPowerOf2_32(VScaleMin))
      CheckFailed("'vscale_range' minimum must be power-of-two value", V);
    std::optional<unsigned> VScaleMax = Attrs.getFnAttrs().getVScaleRangeMax();
    if (VScaleMax && VScaleMin > VScaleMax)
      CheckFailed("'vscale_range' minimum cannot be greater than maximum", V);
    else if (VScaleMax && !isPowerOf2_32(*VScaleMax))
      CheckFailed("'vscale_range' maximum must be power-of-two value", V);
  }

  if (Attrs.hasFnAttr("frame-pointer")) {
    StringRef FP = Attrs.getFnAttr("frame-pointer").getValueAsString();
    if (FP != "all" && FP != "non-leaf" && FP != "none" && FP != "reserved")
      CheckFailed("invalid value for 'frame-pointer' attribute: " + FP, V);
  }

  // Check EVEX512 feature.
  if (MaxParameterWidth >= 512 && Attrs.hasFnAttr("target-features") &&
      TT.isX86()) {
    StringRef TF = Attrs.getFnAttr("target-features").getValueAsString();
    Check(!TF.contains("+avx512f") || !TF.contains("-evex512"),
          "512-bit vector arguments require 'evex512' for AVX512", V);
  }

  checkUnsignedBaseTenFuncAttr(Attrs, "patchable-function-prefix", V);
  checkUnsignedBaseTenFuncAttr(Attrs, "patchable-function-entry", V);
  checkUnsignedBaseTenFuncAttr(Attrs, "warn-stack-size", V);

  if (auto A = Attrs.getFnAttr("sign-return-address"); A.isValid()) {
    StringRef S = A.getValueAsString();
    if (S != "none" && S != "all" && S != "non-leaf")
      CheckFailed("invalid value for 'sign-return-address' attribute: " + S, V);
  }

  if (auto A = Attrs.getFnAttr("sign-return-address-key"); A.isValid()) {
    StringRef S = A.getValueAsString();
    if (S != "a_key" && S != "b_key")
      CheckFailed("invalid value for 'sign-return-address-key' attribute: " + S,
                  V);
    if (auto AA = Attrs.getFnAttr("sign-return-address"); !AA.isValid()) {
      CheckFailed(
          "'sign-return-address-key' present without `sign-return-address`");
    }
  }

  if (auto A = Attrs.getFnAttr("branch-target-enforcement"); A.isValid()) {
    StringRef S = A.getValueAsString();
    if (S != "" && S != "true" && S != "false")
      CheckFailed(
          "invalid value for 'branch-target-enforcement' attribute: " + S, V);
  }

  if (auto A = Attrs.getFnAttr("branch-protection-pauth-lr"); A.isValid()) {
    StringRef S = A.getValueAsString();
    if (S != "" && S != "true" && S != "false")
      CheckFailed(
          "invalid value for 'branch-protection-pauth-lr' attribute: " + S, V);
  }

  if (auto A = Attrs.getFnAttr("guarded-control-stack"); A.isValid()) {
    StringRef S = A.getValueAsString();
    if (S != "" && S != "true" && S != "false")
      CheckFailed("invalid value for 'guarded-control-stack' attribute: " + S,
                  V);
  }

  if (auto A = Attrs.getFnAttr("vector-function-abi-variant"); A.isValid()) {
    StringRef S = A.getValueAsString();
    const std::optional<VFInfo> Info = VFABI::tryDemangleForVFABI(S, FT);
    if (!Info)
      CheckFailed("invalid name for a VFABI variant: " + S, V);
  }
}

void Verifier::verifyFunctionMetadata(
    ArrayRef<std::pair<unsigned, MDNode *>> MDs) {
  for (const auto &Pair : MDs) {
    if (Pair.first == LLVMContext::MD_prof) {
      MDNode *MD = Pair.second;
      Check(MD->getNumOperands() >= 2,
            "!prof annotations should have no less than 2 operands", MD);

      // Check first operand.
      Check(MD->getOperand(0) != nullptr, "first operand should not be null",
            MD);
      Check(isa<MDString>(MD->getOperand(0)),
            "expected string with name of the !prof annotation", MD);
      MDString *MDS = cast<MDString>(MD->getOperand(0));
      StringRef ProfName = MDS->getString();
      Check(ProfName == "function_entry_count" ||
                ProfName == "synthetic_function_entry_count",
            "first operand should be 'function_entry_count'"
            " or 'synthetic_function_entry_count'",
            MD);

      // Check second operand.
      Check(MD->getOperand(1) != nullptr, "second operand should not be null",
            MD);
      Check(isa<ConstantAsMetadata>(MD->getOperand(1)),
            "expected integer argument to function_entry_count", MD);
    } else if (Pair.first == LLVMContext::MD_kcfi_type) {
      MDNode *MD = Pair.second;
      Check(MD->getNumOperands() == 1,
            "!kcfi_type must have exactly one operand", MD);
      Check(MD->getOperand(0) != nullptr, "!kcfi_type operand must not be null",
            MD);
      Check(isa<ConstantAsMetadata>(MD->getOperand(0)),
            "expected a constant operand for !kcfi_type", MD);
      Constant *C = cast<ConstantAsMetadata>(MD->getOperand(0))->getValue();
      Check(isa<ConstantInt>(C) && isa<IntegerType>(C->getType()),
            "expected a constant integer operand for !kcfi_type", MD);
      Check(cast<ConstantInt>(C)->getBitWidth() == 32,
            "expected a 32-bit integer constant operand for !kcfi_type", MD);
    }
  }
}

void Verifier::visitConstantExprsRecursively(const Constant *EntryC) {
  if (!ConstantExprVisited.insert(EntryC).second)
    return;

  SmallVector<const Constant *, 16> Stack;
  Stack.push_back(EntryC);

  while (!Stack.empty()) {
    const Constant *C = Stack.pop_back_val();

    // Check this constant expression.
    if (const auto *CE = dyn_cast<ConstantExpr>(C))
      visitConstantExpr(CE);

    if (const auto *CPA = dyn_cast<ConstantPtrAuth>(C))
      visitConstantPtrAuth(CPA);

    if (const auto *GV = dyn_cast<GlobalValue>(C)) {
      // Global Values get visited separately, but we do need to make sure
      // that the global value is in the correct module
      Check(GV->getParent() == &M, "Referencing global in another module!",
            EntryC, &M, GV, GV->getParent());
      continue;
    }

    // Visit all sub-expressions.
    for (const Use &U : C->operands()) {
      const auto *OpC = dyn_cast<Constant>(U);
      if (!OpC)
        continue;
      if (!ConstantExprVisited.insert(OpC).second)
        continue;
      Stack.push_back(OpC);
    }
  }
}

void Verifier::visitConstantExpr(const ConstantExpr *CE) {
  if (CE->getOpcode() == Instruction::BitCast)
    Check(CastInst::castIsValid(Instruction::BitCast, CE->getOperand(0),
                                CE->getType()),
          "Invalid bitcast", CE);
}

void Verifier::visitConstantPtrAuth(const ConstantPtrAuth *CPA) {
  Check(CPA->getPointer()->getType()->isPointerTy(),
        "signed ptrauth constant base pointer must have pointer type");

  Check(CPA->getType() == CPA->getPointer()->getType(),
        "signed ptrauth constant must have same type as its base pointer");

  Check(CPA->getKey()->getBitWidth() == 32,
        "signed ptrauth constant key must be i32 constant integer");

  Check(CPA->getAddrDiscriminator()->getType()->isPointerTy(),
        "signed ptrauth constant address discriminator must be a pointer");

  Check(CPA->getDiscriminator()->getBitWidth() == 64,
        "signed ptrauth constant discriminator must be i64 constant integer");
}

bool Verifier::verifyAttributeCount(AttributeList Attrs, unsigned Params) {
  // There shouldn't be more attribute sets than there are parameters plus the
  // function and return value.
  return Attrs.getNumAttrSets() <= Params + 2;
}

void Verifier::verifyInlineAsmCall(const CallBase &Call) {
  const InlineAsm *IA = cast<InlineAsm>(Call.getCalledOperand());
  unsigned ArgNo = 0;
  unsigned LabelNo = 0;
  for (const InlineAsm::ConstraintInfo &CI : IA->ParseConstraints()) {
    if (CI.Type == InlineAsm::isLabel) {
      ++LabelNo;
      continue;
    }

    // Only deal with constraints that correspond to call arguments.
    if (!CI.hasArg())
      continue;

    if (CI.isIndirect) {
      const Value *Arg = Call.getArgOperand(ArgNo);
      Check(Arg->getType()->isPointerTy(),
            "Operand for indirect constraint must have pointer type", &Call);

      Check(Call.getParamElementType(ArgNo),
            "Operand for indirect constraint must have elementtype attribute",
            &Call);
    } else {
      Check(!Call.paramHasAttr(ArgNo, Attribute::ElementType),
            "Elementtype attribute can only be applied for indirect "
            "constraints",
            &Call);
    }

    ArgNo++;
  }

  if (auto *CallBr = dyn_cast<CallBrInst>(&Call)) {
    Check(LabelNo == CallBr->getNumIndirectDests(),
          "Number of label constraints does not match number of callbr dests",
          &Call);
  } else {
    Check(LabelNo == 0, "Label constraints can only be used with callbr",
          &Call);
  }
}

/// Verify that statepoint intrinsic is well formed.
void Verifier::verifyStatepoint(const CallBase &Call) {
  assert(Call.getCalledFunction() &&
         Call.getCalledFunction()->getIntrinsicID() ==
             Intrinsic::experimental_gc_statepoint);

  Check(!Call.doesNotAccessMemory() && !Call.onlyReadsMemory() &&
            !Call.onlyAccessesArgMemory(),
        "gc.statepoint must read and write all memory to preserve "
        "reordering restrictions required by safepoint semantics",
        Call);

  const int64_t NumPatchBytes =
      cast<ConstantInt>(Call.getArgOperand(1))->getSExtValue();
  assert(isInt<32>(NumPatchBytes) && "NumPatchBytesV is an i32!");
  Check(NumPatchBytes >= 0,
        "gc.statepoint number of patchable bytes must be "
        "positive",
        Call);

  Type *TargetElemType = Call.getParamElementType(2);
  Check(TargetElemType,
        "gc.statepoint callee argument must have elementtype attribute", Call);
  FunctionType *TargetFuncType = dyn_cast<FunctionType>(TargetElemType);
  Check(TargetFuncType,
        "gc.statepoint callee elementtype must be function type", Call);

  const int NumCallArgs = cast<ConstantInt>(Call.getArgOperand(3))->getZExtValue();
  Check(NumCallArgs >= 0,
        "gc.statepoint number of arguments to underlying call "
        "must be positive",
        Call);
  const int NumParams = (int)TargetFuncType->getNumParams();
  if (TargetFuncType->isVarArg()) {
    Check(NumCallArgs >= NumParams,
          "gc.statepoint mismatch in number of vararg call args", Call);

    // TODO: Remove this limitation
    Check(TargetFuncType->getReturnType()->isVoidTy(),
          "gc.statepoint doesn't support wrapping non-void "
          "vararg functions yet",
          Call);
  } else
    Check(NumCallArgs == NumParams,
          "gc.statepoint mismatch in number of call args", Call);

  const uint64_t Flags
    = cast<ConstantInt>(Call.getArgOperand(4))->getZExtValue();
  Check((Flags & ~(uint64_t)StatepointFlags::MaskAll) == 0,
        "unknown flag used in gc.statepoint flags argument", Call);

  // Verify that the types of the call parameter arguments match
  // the type of the wrapped callee.
  AttributeList Attrs = Call.getAttributes();
  for (int i = 0; i < NumParams; i++) {
    Type *ParamType = TargetFuncType->getParamType(i);
    Type *ArgType = Call.getArgOperand(5 + i)->getType();
    Check(ArgType == ParamType,
          "gc.statepoint call argument does not match wrapped "
          "function type",
          Call);

    if (TargetFuncType->isVarArg()) {
      AttributeSet ArgAttrs = Attrs.getParamAttrs(5 + i);
      Check(!ArgAttrs.hasAttribute(Attribute::StructRet),
            "Attribute 'sret' cannot be used for vararg call arguments!", Call);
    }
  }

  const int EndCallArgsInx = 4 + NumCallArgs;

  const Value *NumTransitionArgsV = Call.getArgOperand(EndCallArgsInx + 1);
  Check(isa<ConstantInt>(NumTransitionArgsV),
        "gc.statepoint number of transition arguments "
        "must be constant integer",
        Call);
  const int NumTransitionArgs =
      cast<ConstantInt>(NumTransitionArgsV)->getZExtValue();
  Check(NumTransitionArgs == 0,
        "gc.statepoint w/inline transition bundle is deprecated", Call);
  const int EndTransitionArgsInx = EndCallArgsInx + 1 + NumTransitionArgs;

  const Value *NumDeoptArgsV = Call.getArgOperand(EndTransitionArgsInx + 1);
  Check(isa<ConstantInt>(NumDeoptArgsV),
        "gc.statepoint number of deoptimization arguments "
        "must be constant integer",
        Call);
  const int NumDeoptArgs = cast<ConstantInt>(NumDeoptArgsV)->getZExtValue();
  Check(NumDeoptArgs == 0,
        "gc.statepoint w/inline deopt operands is deprecated", Call);

  const int ExpectedNumArgs = 7 + NumCallArgs;
  Check(ExpectedNumArgs == (int)Call.arg_size(),
        "gc.statepoint too many arguments", Call);

  // Check that the only uses of this gc.statepoint are gc.result or
  // gc.relocate calls which are tied to this statepoint and thus part
  // of the same statepoint sequence
  for (const User *U : Call.users()) {
    const CallInst *UserCall = dyn_cast<const CallInst>(U);
    Check(UserCall, "illegal use of statepoint token", Call, U);
    if (!UserCall)
      continue;
    Check(isa<GCRelocateInst>(UserCall) || isa<GCResultInst>(UserCall),
          "gc.result or gc.relocate are the only value uses "
          "of a gc.statepoint",
          Call, U);
    if (isa<GCResultInst>(UserCall)) {
      Check(UserCall->getArgOperand(0) == &Call,
            "gc.result connected to wrong gc.statepoint", Call, UserCall);
    } else if (isa<GCRelocateInst>(Call)) {
      Check(UserCall->getArgOperand(0) == &Call,
            "gc.relocate connected to wrong gc.statepoint", Call, UserCall);
    }
  }

  // Note: It is legal for a single derived pointer to be listed multiple
  // times.  It's non-optimal, but it is legal.  It can also happen after
  // insertion if we strip a bitcast away.
  // Note: It is really tempting to check that each base is relocated and
  // that a derived pointer is never reused as a base pointer.  This turns
  // out to be problematic since optimizations run after safepoint insertion
  // can recognize equality properties that the insertion logic doesn't know
  // about.  See example statepoint.ll in the verifier subdirectory
}

void Verifier::verifyFrameRecoverIndices() {
  for (auto &Counts : FrameEscapeInfo) {
    Function *F = Counts.first;
    unsigned EscapedObjectCount = Counts.second.first;
    unsigned MaxRecoveredIndex = Counts.second.second;
    Check(MaxRecoveredIndex <= EscapedObjectCount,
          "all indices passed to llvm.localrecover must be less than the "
          "number of arguments passed to llvm.localescape in the parent "
          "function",
          F);
  }
}

static Instruction *getSuccPad(Instruction *Terminator) {
  BasicBlock *UnwindDest;
  if (auto *II = dyn_cast<InvokeInst>(Terminator))
    UnwindDest = II->getUnwindDest();
  else if (auto *CSI = dyn_cast<CatchSwitchInst>(Terminator))
    UnwindDest = CSI->getUnwindDest();
  else
    UnwindDest = cast<CleanupReturnInst>(Terminator)->getUnwindDest();
  return UnwindDest->getFirstNonPHI();
}

void Verifier::verifySiblingFuncletUnwinds() {
  SmallPtrSet<Instruction *, 8> Visited;
  SmallPtrSet<Instruction *, 8> Active;
  for (const auto &Pair : SiblingFuncletInfo) {
    Instruction *PredPad = Pair.first;
    if (Visited.count(PredPad))
      continue;
    Active.insert(PredPad);
    Instruction *Terminator = Pair.second;
    do {
      Instruction *SuccPad = getSuccPad(Terminator);
      if (Active.count(SuccPad)) {
        // Found a cycle; report error
        Instruction *CyclePad = SuccPad;
        SmallVector<Instruction *, 8> CycleNodes;
        do {
          CycleNodes.push_back(CyclePad);
          Instruction *CycleTerminator = SiblingFuncletInfo[CyclePad];
          if (CycleTerminator != CyclePad)
            CycleNodes.push_back(CycleTerminator);
          CyclePad = getSuccPad(CycleTerminator);
        } while (CyclePad != SuccPad);
        Check(false, "EH pads can't handle each other's exceptions",
              ArrayRef<Instruction *>(CycleNodes));
      }
      // Don't re-walk a node we've already checked
      if (!Visited.insert(SuccPad).second)
        break;
      // Walk to this successor if it has a map entry.
      PredPad = SuccPad;
      auto TermI = SiblingFuncletInfo.find(PredPad);
      if (TermI == SiblingFuncletInfo.end())
        break;
      Terminator = TermI->second;
      Active.insert(PredPad);
    } while (true);
    // Each node only has one successor, so we've walked all the active
    // nodes' successors.
    Active.clear();
  }
}

// visitFunction - Verify that a function is ok.
//
void Verifier::visitFunction(const Function &F) {
  visitGlobalValue(F);

  // Check function arguments.
  FunctionType *FT = F.getFunctionType();
  unsigned NumArgs = F.arg_size();

  Check(&Context == &F.getContext(),
        "Function context does not match Module context!", &F);

  Check(!F.hasCommonLinkage(), "Functions may not have common linkage", &F);
  Check(FT->getNumParams() == NumArgs,
        "# formal arguments must match # of arguments for function type!", &F,
        FT);
  Check(F.getReturnType()->isFirstClassType() ||
            F.getReturnType()->isVoidTy() || F.getReturnType()->isStructTy(),
        "Functions cannot return aggregate values!", &F);

  Check(!F.hasStructRetAttr() || F.getReturnType()->isVoidTy(),
        "Invalid struct return type!", &F);

  AttributeList Attrs = F.getAttributes();

  Check(verifyAttributeCount(Attrs, FT->getNumParams()),
        "Attribute after last parameter!", &F);

  CheckDI(F.IsNewDbgInfoFormat == F.getParent()->IsNewDbgInfoFormat,
          "Function debug format should match parent module", &F,
          F.IsNewDbgInfoFormat, F.getParent(),
          F.getParent()->IsNewDbgInfoFormat);

  bool IsIntrinsic = F.isIntrinsic();

  // Check function attributes.
  verifyFunctionAttrs(FT, Attrs, &F, IsIntrinsic, /* IsInlineAsm */ false);

  // On function declarations/definitions, we do not support the builtin
  // attribute. We do not check this in VerifyFunctionAttrs since that is
  // checking for Attributes that can/can not ever be on functions.
  Check(!Attrs.hasFnAttr(Attribute::Builtin),
        "Attribute 'builtin' can only be applied to a callsite.", &F);

  Check(!Attrs.hasAttrSomewhere(Attribute::ElementType),
        "Attribute 'elementtype' can only be applied to a callsite.", &F);

  // Check that this function meets the restrictions on this calling convention.
  // Sometimes varargs is used for perfectly forwarding thunks, so some of these
  // restrictions can be lifted.
  switch (F.getCallingConv()) {
  default:
  case CallingConv::C:
    break;
  case CallingConv::X86_INTR: {
    Check(F.arg_empty() || Attrs.hasParamAttr(0, Attribute::ByVal),
          "Calling convention parameter requires byval", &F);
    break;
  }
  case CallingConv::AMDGPU_KERNEL:
  case CallingConv::SPIR_KERNEL:
  case CallingConv::AMDGPU_CS_Chain:
  case CallingConv::AMDGPU_CS_ChainPreserve:
    Check(F.getReturnType()->isVoidTy(),
          "Calling convention requires void return type", &F);
    [[fallthrough]];
  case CallingConv::AMDGPU_VS:
  case CallingConv::AMDGPU_HS:
  case CallingConv::AMDGPU_GS:
  case CallingConv::AMDGPU_PS:
  case CallingConv::AMDGPU_CS:
    Check(!F.hasStructRetAttr(), "Calling convention does not allow sret", &F);
    if (F.getCallingConv() != CallingConv::SPIR_KERNEL) {
      const unsigned StackAS = DL.getAllocaAddrSpace();
      unsigned i = 0;
      for (const Argument &Arg : F.args()) {
        Check(!Attrs.hasParamAttr(i, Attribute::ByVal),
              "Calling convention disallows byval", &F);
        Check(!Attrs.hasParamAttr(i, Attribute::Preallocated),
              "Calling convention disallows preallocated", &F);
        Check(!Attrs.hasParamAttr(i, Attribute::InAlloca),
              "Calling convention disallows inalloca", &F);

        if (Attrs.hasParamAttr(i, Attribute::ByRef)) {
          // FIXME: Should also disallow LDS and GDS, but we don't have the enum
          // value here.
          Check(Arg.getType()->getPointerAddressSpace() != StackAS,
                "Calling convention disallows stack byref", &F);
        }

        ++i;
      }
    }

    [[fallthrough]];
  case CallingConv::Fast:
  case CallingConv::Cold:
  case CallingConv::Intel_OCL_BI:
  case CallingConv::PTX_Kernel:
  case CallingConv::PTX_Device:
    Check(!F.isVarArg(),
          "Calling convention does not support varargs or "
          "perfect forwarding!",
          &F);
    break;
  }

  // Check that the argument values match the function type for this function...
  unsigned i = 0;
  for (const Argument &Arg : F.args()) {
    Check(Arg.getType() == FT->getParamType(i),
          "Argument value does not match function argument type!", &Arg,
          FT->getParamType(i));
    Check(Arg.getType()->isFirstClassType(),
          "Function arguments must have first-class types!", &Arg);
    if (!IsIntrinsic) {
      Check(!Arg.getType()->isMetadataTy(),
            "Function takes metadata but isn't an intrinsic", &Arg, &F);
      Check(!Arg.getType()->isTokenTy(),
            "Function takes token but isn't an intrinsic", &Arg, &F);
      Check(!Arg.getType()->isX86_AMXTy(),
            "Function takes x86_amx but isn't an intrinsic", &Arg, &F);
    }

    // Check that swifterror argument is only used by loads and stores.
    if (Attrs.hasParamAttr(i, Attribute::SwiftError)) {
      verifySwiftErrorValue(&Arg);
    }
    ++i;
  }

  if (!IsIntrinsic) {
    Check(!F.getReturnType()->isTokenTy(),
          "Function returns a token but isn't an intrinsic", &F);
    Check(!F.getReturnType()->isX86_AMXTy(),
          "Function returns a x86_amx but isn't an intrinsic", &F);
  }

  // Get the function metadata attachments.
  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  F.getAllMetadata(MDs);
  assert(F.hasMetadata() != MDs.empty() && "Bit out-of-sync");
  verifyFunctionMetadata(MDs);

  // Check validity of the personality function
  if (F.hasPersonalityFn()) {
    auto *Per = dyn_cast<Function>(F.getPersonalityFn()->stripPointerCasts());
    if (Per)
      Check(Per->getParent() == F.getParent(),
            "Referencing personality function in another module!", &F,
            F.getParent(), Per, Per->getParent());
  }

  // EH funclet coloring can be expensive, recompute on-demand
  BlockEHFuncletColors.clear();

  if (F.isMaterializable()) {
    // Function has a body somewhere we can't see.
    Check(MDs.empty(), "unmaterialized function cannot have metadata", &F,
          MDs.empty() ? nullptr : MDs.front().second);
  } else if (F.isDeclaration()) {
    for (const auto &I : MDs) {
      // This is used for call site debug information.
      CheckDI(I.first != LLVMContext::MD_dbg ||
                  !cast<DISubprogram>(I.second)->isDistinct(),
              "function declaration may only have a unique !dbg attachment",
              &F);
      Check(I.first != LLVMContext::MD_prof,
            "function declaration may not have a !prof attachment", &F);

      // Verify the metadata itself.
      visitMDNode(*I.second, AreDebugLocsAllowed::Yes);
    }
    Check(!F.hasPersonalityFn(),
          "Function declaration shouldn't have a personality routine", &F);
  } else {
    // Verify that this function (which has a body) is not named "llvm.*".  It
    // is not legal to define intrinsics.
    Check(!IsIntrinsic, "llvm intrinsics cannot be defined!", &F);

    // Check the entry node
    const BasicBlock *Entry = &F.getEntryBlock();
    Check(pred_empty(Entry),
          "Entry block to function must not have predecessors!", Entry);

    // The address of the entry block cannot be taken, unless it is dead.
    if (Entry->hasAddressTaken()) {
      Check(!BlockAddress::lookup(Entry)->isConstantUsed(),
            "blockaddress may not be used with the entry block!", Entry);
    }

    unsigned NumDebugAttachments = 0, NumProfAttachments = 0,
             NumKCFIAttachments = 0;
    // Visit metadata attachments.
    for (const auto &I : MDs) {
      // Verify that the attachment is legal.
      auto AllowLocs = AreDebugLocsAllowed::No;
      switch (I.first) {
      default:
        break;
      case LLVMContext::MD_dbg: {
        ++NumDebugAttachments;
        CheckDI(NumDebugAttachments == 1,
                "function must have a single !dbg attachment", &F, I.second);
        CheckDI(isa<DISubprogram>(I.second),
                "function !dbg attachment must be a subprogram", &F, I.second);
        CheckDI(cast<DISubprogram>(I.second)->isDistinct(),
                "function definition may only have a distinct !dbg attachment",
                &F);

        auto *SP = cast<DISubprogram>(I.second);
        const Function *&AttachedTo = DISubprogramAttachments[SP];
        CheckDI(!AttachedTo || AttachedTo == &F,
                "DISubprogram attached to more than one function", SP, &F);
        AttachedTo = &F;
        AllowLocs = AreDebugLocsAllowed::Yes;
        break;
      }
      case LLVMContext::MD_prof:
        ++NumProfAttachments;
        Check(NumProfAttachments == 1,
              "function must have a single !prof attachment", &F, I.second);
        break;
      case LLVMContext::MD_kcfi_type:
        ++NumKCFIAttachments;
        Check(NumKCFIAttachments == 1,
              "function must have a single !kcfi_type attachment", &F,
              I.second);
        break;
      }

      // Verify the metadata itself.
      visitMDNode(*I.second, AllowLocs);
    }
  }

  // If this function is actually an intrinsic, verify that it is only used in
  // direct call/invokes, never having its "address taken".
  // Only do this if the module is materialized, otherwise we don't have all the
  // uses.
  if (F.isIntrinsic() && F.getParent()->isMaterialized()) {
    const User *U;
    if (F.hasAddressTaken(&U, false, true, false,
                          /*IgnoreARCAttachedCall=*/true))
      Check(false, "Invalid user of intrinsic instruction!", U);
  }

  // Check intrinsics' signatures.
  switch (F.getIntrinsicID()) {
  case Intrinsic::experimental_gc_get_pointer_base: {
    FunctionType *FT = F.getFunctionType();
    Check(FT->getNumParams() == 1, "wrong number of parameters", F);
    Check(isa<PointerType>(F.getReturnType()),
          "gc.get.pointer.base must return a pointer", F);
    Check(FT->getParamType(0) == F.getReturnType(),
          "gc.get.pointer.base operand and result must be of the same type", F);
    break;
  }
  case Intrinsic::experimental_gc_get_pointer_offset: {
    FunctionType *FT = F.getFunctionType();
    Check(FT->getNumParams() == 1, "wrong number of parameters", F);
    Check(isa<PointerType>(FT->getParamType(0)),
          "gc.get.pointer.offset operand must be a pointer", F);
    Check(F.getReturnType()->isIntegerTy(),
          "gc.get.pointer.offset must return integer", F);
    break;
  }
  }

  auto *N = F.getSubprogram();
  HasDebugInfo = (N != nullptr);
  if (!HasDebugInfo)
    return;

  // Check that all !dbg attachments lead to back to N.
  //
  // FIXME: Check this incrementally while visiting !dbg attachments.
  // FIXME: Only check when N is the canonical subprogram for F.
  SmallPtrSet<const MDNode *, 32> Seen;
  auto VisitDebugLoc = [&](const Instruction &I, const MDNode *Node) {
    // Be careful about using DILocation here since we might be dealing with
    // broken code (this is the Verifier after all).
    const DILocation *DL = dyn_cast_or_null<DILocation>(Node);
    if (!DL)
      return;
    if (!Seen.insert(DL).second)
      return;

    Metadata *Parent = DL->getRawScope();
    CheckDI(Parent && isa<DILocalScope>(Parent),
            "DILocation's scope must be a DILocalScope", N, &F, &I, DL, Parent);

    DILocalScope *Scope = DL->getInlinedAtScope();
    Check(Scope, "Failed to find DILocalScope", DL);

    if (!Seen.insert(Scope).second)
      return;

    DISubprogram *SP = Scope->getSubprogram();

    // Scope and SP could be the same MDNode and we don't want to skip
    // validation in that case
    if (SP && ((Scope != SP) && !Seen.insert(SP).second))
      return;

    CheckDI(SP->describes(&F),
            "!dbg attachment points at wrong subprogram for function", N, &F,
            &I, DL, Scope, SP);
  };
  for (auto &BB : F)
    for (auto &I : BB) {
      VisitDebugLoc(I, I.getDebugLoc().getAsMDNode());
      // The llvm.loop annotations also contain two DILocations.
      if (auto MD = I.getMetadata(LLVMContext::MD_loop))
        for (unsigned i = 1; i < MD->getNumOperands(); ++i)
          VisitDebugLoc(I, dyn_cast_or_null<MDNode>(MD->getOperand(i)));
      if (BrokenDebugInfo)
        return;
    }
}

// verifyBasicBlock - Verify that a basic block is well formed...
//
void Verifier::visitBasicBlock(BasicBlock &BB) {
  InstsInThisBlock.clear();
  ConvergenceVerifyHelper.visit(BB);

  // Ensure that basic blocks have terminators!
  Check(BB.getTerminator(), "Basic Block does not have terminator!", &BB);

  // Check constraints that this basic block imposes on all of the PHI nodes in
  // it.
  if (isa<PHINode>(BB.front())) {
    SmallVector<BasicBlock *, 8> Preds(predecessors(&BB));
    SmallVector<std::pair<BasicBlock*, Value*>, 8> Values;
    llvm::sort(Preds);
    for (const PHINode &PN : BB.phis()) {
      Check(PN.getNumIncomingValues() == Preds.size(),
            "PHINode should have one entry for each predecessor of its "
            "parent basic block!",
            &PN);

      // Get and sort all incoming values in the PHI node...
      Values.clear();
      Values.reserve(PN.getNumIncomingValues());
      for (unsigned i = 0, e = PN.getNumIncomingValues(); i != e; ++i)
        Values.push_back(
            std::make_pair(PN.getIncomingBlock(i), PN.getIncomingValue(i)));
      llvm::sort(Values);

      for (unsigned i = 0, e = Values.size(); i != e; ++i) {
        // Check to make sure that if there is more than one entry for a
        // particular basic block in this PHI node, that the incoming values are
        // all identical.
        //
        Check(i == 0 || Values[i].first != Values[i - 1].first ||
                  Values[i].second == Values[i - 1].second,
              "PHI node has multiple entries for the same basic block with "
              "different incoming values!",
              &PN, Values[i].first, Values[i].second, Values[i - 1].second);

        // Check to make sure that the predecessors and PHI node entries are
        // matched up.
        Check(Values[i].first == Preds[i],
              "PHI node entries do not match predecessors!", &PN,
              Values[i].first, Preds[i]);
      }
    }
  }

  // Check that all instructions have their parent pointers set up correctly.
  for (auto &I : BB)
  {
    Check(I.getParent() == &BB, "Instruction has bogus parent pointer!");
  }

  CheckDI(BB.IsNewDbgInfoFormat == BB.getParent()->IsNewDbgInfoFormat,
          "BB debug format should match parent function", &BB,
          BB.IsNewDbgInfoFormat, BB.getParent(),
          BB.getParent()->IsNewDbgInfoFormat);

  // Confirm that no issues arise from the debug program.
  if (BB.IsNewDbgInfoFormat)
    CheckDI(!BB.getTrailingDbgRecords(), "Basic Block has trailing DbgRecords!",
            &BB);
}

void Verifier::visitTerminator(Instruction &I) {
  // Ensure that terminators only exist at the end of the basic block.
  Check(&I == I.getParent()->getTerminator(),
        "Terminator found in the middle of a basic block!", I.getParent());
  visitInstruction(I);
}

void Verifier::visitBranchInst(BranchInst &BI) {
  if (BI.isConditional()) {
    Check(BI.getCondition()->getType()->isIntegerTy(1),
          "Branch condition is not 'i1' type!", &BI, BI.getCondition());
  }
  visitTerminator(BI);
}

void Verifier::visitReturnInst(ReturnInst &RI) {
  Function *F = RI.getParent()->getParent();
  unsigned N = RI.getNumOperands();
  if (F->getReturnType()->isVoidTy())
    Check(N == 0,
          "Found return instr that returns non-void in Function of void "
          "return type!",
          &RI, F->getReturnType());
  else
    Check(N == 1 && F->getReturnType() == RI.getOperand(0)->getType(),
          "Function return type does not match operand "
          "type of return inst!",
          &RI, F->getReturnType());

  // Check to make sure that the return value has necessary properties for
  // terminators...
  visitTerminator(RI);
}

void Verifier::visitSwitchInst(SwitchInst &SI) {
  Check(SI.getType()->isVoidTy(), "Switch must have void result type!", &SI);
  // Check to make sure that all of the constants in the switch instruction
  // have the same type as the switched-on value.
  Type *SwitchTy = SI.getCondition()->getType();
  SmallPtrSet<ConstantInt*, 32> Constants;
  for (auto &Case : SI.cases()) {
    Check(isa<ConstantInt>(SI.getOperand(Case.getCaseIndex() * 2 + 2)),
          "Case value is not a constant integer.", &SI);
    Check(Case.getCaseValue()->getType() == SwitchTy,
          "Switch constants must all be same type as switch value!", &SI);
    Check(Constants.insert(Case.getCaseValue()).second,
          "Duplicate integer as switch case", &SI, Case.getCaseValue());
  }

  visitTerminator(SI);
}

void Verifier::visitIndirectBrInst(IndirectBrInst &BI) {
  Check(BI.getAddress()->getType()->isPointerTy(),
        "Indirectbr operand must have pointer type!", &BI);
  for (unsigned i = 0, e = BI.getNumDestinations(); i != e; ++i)
    Check(BI.getDestination(i)->getType()->isLabelTy(),
          "Indirectbr destinations must all have pointer type!", &BI);

  visitTerminator(BI);
}

void Verifier::visitCallBrInst(CallBrInst &CBI) {
  Check(CBI.isInlineAsm(), "Callbr is currently only used for asm-goto!", &CBI);
  const InlineAsm *IA = cast<InlineAsm>(CBI.getCalledOperand());
  Check(!IA->canThrow(), "Unwinding from Callbr is not allowed");

  verifyInlineAsmCall(CBI);
  visitTerminator(CBI);
}

void Verifier::visitSelectInst(SelectInst &SI) {
  Check(!SelectInst::areInvalidOperands(SI.getOperand(0), SI.getOperand(1),
                                        SI.getOperand(2)),
        "Invalid operands for select instruction!", &SI);

  Check(SI.getTrueValue()->getType() == SI.getType(),
        "Select values must have same type as select instruction!", &SI);
  visitInstruction(SI);
}

/// visitUserOp1 - User defined operators shouldn't live beyond the lifetime of
/// a pass, if any exist, it's an error.
///
void Verifier::visitUserOp1(Instruction &I) {
  Check(false, "User-defined operators should not live outside of a pass!", &I);
}

void Verifier::visitTruncInst(TruncInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  // Get the size of the types in bits, we'll need this later
  unsigned SrcBitSize = SrcTy->getScalarSizeInBits();
  unsigned DestBitSize = DestTy->getScalarSizeInBits();

  Check(SrcTy->isIntOrIntVectorTy(), "Trunc only operates on integer", &I);
  Check(DestTy->isIntOrIntVectorTy(), "Trunc only produces integer", &I);
  Check(SrcTy->isVectorTy() == DestTy->isVectorTy(),
        "trunc source and destination must both be a vector or neither", &I);
  Check(SrcBitSize > DestBitSize, "DestTy too big for Trunc", &I);

  visitInstruction(I);
}

void Verifier::visitZExtInst(ZExtInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  // Get the size of the types in bits, we'll need this later
  Check(SrcTy->isIntOrIntVectorTy(), "ZExt only operates on integer", &I);
  Check(DestTy->isIntOrIntVectorTy(), "ZExt only produces an integer", &I);
  Check(SrcTy->isVectorTy() == DestTy->isVectorTy(),
        "zext source and destination must both be a vector or neither", &I);
  unsigned SrcBitSize = SrcTy->getScalarSizeInBits();
  unsigned DestBitSize = DestTy->getScalarSizeInBits();

  Check(SrcBitSize < DestBitSize, "Type too small for ZExt", &I);

  visitInstruction(I);
}

void Verifier::visitSExtInst(SExtInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  // Get the size of the types in bits, we'll need this later
  unsigned SrcBitSize = SrcTy->getScalarSizeInBits();
  unsigned DestBitSize = DestTy->getScalarSizeInBits();

  Check(SrcTy->isIntOrIntVectorTy(), "SExt only operates on integer", &I);
  Check(DestTy->isIntOrIntVectorTy(), "SExt only produces an integer", &I);
  Check(SrcTy->isVectorTy() == DestTy->isVectorTy(),
        "sext source and destination must both be a vector or neither", &I);
  Check(SrcBitSize < DestBitSize, "Type too small for SExt", &I);

  visitInstruction(I);
}

void Verifier::visitFPTruncInst(FPTruncInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();
  // Get the size of the types in bits, we'll need this later
  unsigned SrcBitSize = SrcTy->getScalarSizeInBits();
  unsigned DestBitSize = DestTy->getScalarSizeInBits();

  Check(SrcTy->isFPOrFPVectorTy(), "FPTrunc only operates on FP", &I);
  Check(DestTy->isFPOrFPVectorTy(), "FPTrunc only produces an FP", &I);
  Check(SrcTy->isVectorTy() == DestTy->isVectorTy(),
        "fptrunc source and destination must both be a vector or neither", &I);
  Check(SrcBitSize > DestBitSize, "DestTy too big for FPTrunc", &I);

  visitInstruction(I);
}

void Verifier::visitFPExtInst(FPExtInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  // Get the size of the types in bits, we'll need this later
  unsigned SrcBitSize = SrcTy->getScalarSizeInBits();
  unsigned DestBitSize = DestTy->getScalarSizeInBits();

  Check(SrcTy->isFPOrFPVectorTy(), "FPExt only operates on FP", &I);
  Check(DestTy->isFPOrFPVectorTy(), "FPExt only produces an FP", &I);
  Check(SrcTy->isVectorTy() == DestTy->isVectorTy(),
        "fpext source and destination must both be a vector or neither", &I);
  Check(SrcBitSize < DestBitSize, "DestTy too small for FPExt", &I);

  visitInstruction(I);
}

void Verifier::visitUIToFPInst(UIToFPInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  bool SrcVec = SrcTy->isVectorTy();
  bool DstVec = DestTy->isVectorTy();

  Check(SrcVec == DstVec,
        "UIToFP source and dest must both be vector or scalar", &I);
  Check(SrcTy->isIntOrIntVectorTy(),
        "UIToFP source must be integer or integer vector", &I);
  Check(DestTy->isFPOrFPVectorTy(), "UIToFP result must be FP or FP vector",
        &I);

  if (SrcVec && DstVec)
    Check(cast<VectorType>(SrcTy)->getElementCount() ==
              cast<VectorType>(DestTy)->getElementCount(),
          "UIToFP source and dest vector length mismatch", &I);

  visitInstruction(I);
}

void Verifier::visitSIToFPInst(SIToFPInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  bool SrcVec = SrcTy->isVectorTy();
  bool DstVec = DestTy->isVectorTy();

  Check(SrcVec == DstVec,
        "SIToFP source and dest must both be vector or scalar", &I);
  Check(SrcTy->isIntOrIntVectorTy(),
        "SIToFP source must be integer or integer vector", &I);
  Check(DestTy->isFPOrFPVectorTy(), "SIToFP result must be FP or FP vector",
        &I);

  if (SrcVec && DstVec)
    Check(cast<VectorType>(SrcTy)->getElementCount() ==
              cast<VectorType>(DestTy)->getElementCount(),
          "SIToFP source and dest vector length mismatch", &I);

  visitInstruction(I);
}

void Verifier::visitFPToUIInst(FPToUIInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  bool SrcVec = SrcTy->isVectorTy();
  bool DstVec = DestTy->isVectorTy();

  Check(SrcVec == DstVec,
        "FPToUI source and dest must both be vector or scalar", &I);
  Check(SrcTy->isFPOrFPVectorTy(), "FPToUI source must be FP or FP vector", &I);
  Check(DestTy->isIntOrIntVectorTy(),
        "FPToUI result must be integer or integer vector", &I);

  if (SrcVec && DstVec)
    Check(cast<VectorType>(SrcTy)->getElementCount() ==
              cast<VectorType>(DestTy)->getElementCount(),
          "FPToUI source and dest vector length mismatch", &I);

  visitInstruction(I);
}

void Verifier::visitFPToSIInst(FPToSIInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  bool SrcVec = SrcTy->isVectorTy();
  bool DstVec = DestTy->isVectorTy();

  Check(SrcVec == DstVec,
        "FPToSI source and dest must both be vector or scalar", &I);
  Check(SrcTy->isFPOrFPVectorTy(), "FPToSI source must be FP or FP vector", &I);
  Check(DestTy->isIntOrIntVectorTy(),
        "FPToSI result must be integer or integer vector", &I);

  if (SrcVec && DstVec)
    Check(cast<VectorType>(SrcTy)->getElementCount() ==
              cast<VectorType>(DestTy)->getElementCount(),
          "FPToSI source and dest vector length mismatch", &I);

  visitInstruction(I);
}

void Verifier::visitPtrToIntInst(PtrToIntInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  Check(SrcTy->isPtrOrPtrVectorTy(), "PtrToInt source must be pointer", &I);

  Check(DestTy->isIntOrIntVectorTy(), "PtrToInt result must be integral", &I);
  Check(SrcTy->isVectorTy() == DestTy->isVectorTy(), "PtrToInt type mismatch",
        &I);

  if (SrcTy->isVectorTy()) {
    auto *VSrc = cast<VectorType>(SrcTy);
    auto *VDest = cast<VectorType>(DestTy);
    Check(VSrc->getElementCount() == VDest->getElementCount(),
          "PtrToInt Vector width mismatch", &I);
  }

  visitInstruction(I);
}

void Verifier::visitIntToPtrInst(IntToPtrInst &I) {
  // Get the source and destination types
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  Check(SrcTy->isIntOrIntVectorTy(), "IntToPtr source must be an integral", &I);
  Check(DestTy->isPtrOrPtrVectorTy(), "IntToPtr result must be a pointer", &I);

  Check(SrcTy->isVectorTy() == DestTy->isVectorTy(), "IntToPtr type mismatch",
        &I);
  if (SrcTy->isVectorTy()) {
    auto *VSrc = cast<VectorType>(SrcTy);
    auto *VDest = cast<VectorType>(DestTy);
    Check(VSrc->getElementCount() == VDest->getElementCount(),
          "IntToPtr Vector width mismatch", &I);
  }
  visitInstruction(I);
}

void Verifier::visitBitCastInst(BitCastInst &I) {
  Check(
      CastInst::castIsValid(Instruction::BitCast, I.getOperand(0), I.getType()),
      "Invalid bitcast", &I);
  visitInstruction(I);
}

void Verifier::visitAddrSpaceCastInst(AddrSpaceCastInst &I) {
  Type *SrcTy = I.getOperand(0)->getType();
  Type *DestTy = I.getType();

  Check(SrcTy->isPtrOrPtrVectorTy(), "AddrSpaceCast source must be a pointer",
        &I);
  Check(DestTy->isPtrOrPtrVectorTy(), "AddrSpaceCast result must be a pointer",
        &I);
  Check(SrcTy->getPointerAddressSpace() != DestTy->getPointerAddressSpace(),
        "AddrSpaceCast must be between different address spaces", &I);
  if (auto *SrcVTy = dyn_cast<VectorType>(SrcTy))
    Check(SrcVTy->getElementCount() ==
              cast<VectorType>(DestTy)->getElementCount(),
          "AddrSpaceCast vector pointer number of elements mismatch", &I);
  visitInstruction(I);
}

/// visitPHINode - Ensure that a PHI node is well formed.
///
void Verifier::visitPHINode(PHINode &PN) {
  // Ensure that the PHI nodes are all grouped together at the top of the block.
  // This can be tested by checking whether the instruction before this is
  // either nonexistent (because this is begin()) or is a PHI node.  If not,
  // then there is some other instruction before a PHI.
  Check(&PN == &PN.getParent()->front() ||
            isa<PHINode>(--BasicBlock::iterator(&PN)),
        "PHI nodes not grouped at top of basic block!", &PN, PN.getParent());

  // Check that a PHI doesn't yield a Token.
  Check(!PN.getType()->isTokenTy(), "PHI nodes cannot have token type!");

  // Check that all of the values of the PHI node have the same type as the
  // result.
  for (Value *IncValue : PN.incoming_values()) {
    Check(PN.getType() == IncValue->getType(),
          "PHI node operands are not the same type as the result!", &PN);
  }

  // All other PHI node constraints are checked in the visitBasicBlock method.

  visitInstruction(PN);
}

void Verifier::visitCallBase(CallBase &Call) {
  Check(Call.getCalledOperand()->getType()->isPointerTy(),
        "Called function must be a pointer!", Call);
  FunctionType *FTy = Call.getFunctionType();

  // Verify that the correct number of arguments are being passed
  if (FTy->isVarArg())
    Check(Call.arg_size() >= FTy->getNumParams(),
          "Called function requires more parameters than were provided!", Call);
  else
    Check(Call.arg_size() == FTy->getNumParams(),
          "Incorrect number of arguments passed to called function!", Call);

  // Verify that all arguments to the call match the function type.
  for (unsigned i = 0, e = FTy->getNumParams(); i != e; ++i)
    Check(Call.getArgOperand(i)->getType() == FTy->getParamType(i),
          "Call parameter type does not match function signature!",
          Call.getArgOperand(i), FTy->getParamType(i), Call);

  AttributeList Attrs = Call.getAttributes();

  Check(verifyAttributeCount(Attrs, Call.arg_size()),
        "Attribute after last parameter!", Call);

  Function *Callee =
      dyn_cast<Function>(Call.getCalledOperand()->stripPointerCasts());
  bool IsIntrinsic = Callee && Callee->isIntrinsic();
  if (IsIntrinsic)
    Check(Callee->getValueType() == FTy,
          "Intrinsic called with incompatible signature", Call);

  // Disallow calls to functions with the amdgpu_cs_chain[_preserve] calling
  // convention.
  auto CC = Call.getCallingConv();
  Check(CC != CallingConv::AMDGPU_CS_Chain &&
            CC != CallingConv::AMDGPU_CS_ChainPreserve,
        "Direct calls to amdgpu_cs_chain/amdgpu_cs_chain_preserve functions "
        "not allowed. Please use the @llvm.amdgpu.cs.chain intrinsic instead.",
        Call);

  // Disallow passing/returning values with alignment higher than we can
  // represent.
  // FIXME: Consider making DataLayout cap the alignment, so this isn't
  // necessary.
  auto VerifyTypeAlign = [&](Type *Ty, const Twine &Message) {
    if (!Ty->isSized())
      return;
    Align ABIAlign = DL.getABITypeAlign(Ty);
    Check(ABIAlign.value() <= Value::MaximumAlignment,
          "Incorrect alignment of " + Message + " to called function!", Call);
  };

  if (!IsIntrinsic) {
    VerifyTypeAlign(FTy->getReturnType(), "return type");
    for (unsigned i = 0, e = FTy->getNumParams(); i != e; ++i) {
      Type *Ty = FTy->getParamType(i);
      VerifyTypeAlign(Ty, "argument passed");
    }
  }

  if (Attrs.hasFnAttr(Attribute::Speculatable)) {
    // Don't allow speculatable on call sites, unless the underlying function
    // declaration is also speculatable.
    Check(Callee && Callee->isSpeculatable(),
          "speculatable attribute may not apply to call sites", Call);
  }

  if (Attrs.hasFnAttr(Attribute::Preallocated)) {
    Check(Call.getCalledFunction()->getIntrinsicID() ==
              Intrinsic::call_preallocated_arg,
          "preallocated as a call site attribute can only be on "
          "llvm.call.preallocated.arg");
  }

  // Verify call attributes.
  verifyFunctionAttrs(FTy, Attrs, &Call, IsIntrinsic, Call.isInlineAsm());

  // Conservatively check the inalloca argument.
  // We have a bug if we can find that there is an underlying alloca without
  // inalloca.
  if (Call.hasInAllocaArgument()) {
    Value *InAllocaArg = Call.getArgOperand(FTy->getNumParams() - 1);
    if (auto AI = dyn_cast<AllocaInst>(InAllocaArg->stripInBoundsOffsets()))
      Check(AI->isUsedWithInAlloca(),
            "inalloca argument for call has mismatched alloca", AI, Call);
  }

  // For each argument of the callsite, if it has the swifterror argument,
  // make sure the underlying alloca/parameter it comes from has a swifterror as
  // well.
  for (unsigned i = 0, e = FTy->getNumParams(); i != e; ++i) {
    if (Call.paramHasAttr(i, Attribute::SwiftError)) {
      Value *SwiftErrorArg = Call.getArgOperand(i);
      if (auto AI = dyn_cast<AllocaInst>(SwiftErrorArg->stripInBoundsOffsets())) {
        Check(AI->isSwiftError(),
              "swifterror argument for call has mismatched alloca", AI, Call);
        continue;
      }
      auto ArgI = dyn_cast<Argument>(SwiftErrorArg);
      Check(ArgI, "swifterror argument should come from an alloca or parameter",
            SwiftErrorArg, Call);
      Check(ArgI->hasSwiftErrorAttr(),
            "swifterror argument for call has mismatched parameter", ArgI,
            Call);
    }

    if (Attrs.hasParamAttr(i, Attribute::ImmArg)) {
      // Don't allow immarg on call sites, unless the underlying declaration
      // also has the matching immarg.
      Check(Callee && Callee->hasParamAttribute(i, Attribute::ImmArg),
            "immarg may not apply only to call sites", Call.getArgOperand(i),
            Call);
    }

    if (Call.paramHasAttr(i, Attribute::ImmArg)) {
      Value *ArgVal = Call.getArgOperand(i);
      Check(isa<ConstantInt>(ArgVal) || isa<ConstantFP>(ArgVal),
            "immarg operand has non-immediate parameter", ArgVal, Call);
    }

    if (Call.paramHasAttr(i, Attribute::Preallocated)) {
      Value *ArgVal = Call.getArgOperand(i);
      bool hasOB =
          Call.countOperandBundlesOfType(LLVMContext::OB_preallocated) != 0;
      bool isMustTail = Call.isMustTailCall();
      Check(hasOB != isMustTail,
            "preallocated operand either requires a preallocated bundle or "
            "the call to be musttail (but not both)",
            ArgVal, Call);
    }
  }

  if (FTy->isVarArg()) {
    // FIXME? is 'nest' even legal here?
    bool SawNest = false;
    bool SawReturned = false;

    for (unsigned Idx = 0; Idx < FTy->getNumParams(); ++Idx) {
      if (Attrs.hasParamAttr(Idx, Attribute::Nest))
        SawNest = true;
      if (Attrs.hasParamAttr(Idx, Attribute::Returned))
        SawReturned = true;
    }

    // Check attributes on the varargs part.
    for (unsigned Idx = FTy->getNumParams(); Idx < Call.arg_size(); ++Idx) {
      Type *Ty = Call.getArgOperand(Idx)->getType();
      AttributeSet ArgAttrs = Attrs.getParamAttrs(Idx);
      verifyParameterAttrs(ArgAttrs, Ty, &Call);

      if (ArgAttrs.hasAttribute(Attribute::Nest)) {
        Check(!SawNest, "More than one parameter has attribute nest!", Call);
        SawNest = true;
      }

      if (ArgAttrs.hasAttribute(Attribute::Returned)) {
        Check(!SawReturned, "More than one parameter has attribute returned!",
              Call);
        Check(Ty->canLosslesslyBitCastTo(FTy->getReturnType()),
              "Incompatible argument and return types for 'returned' "
              "attribute",
              Call);
        SawReturned = true;
      }

      // Statepoint intrinsic is vararg but the wrapped function may be not.
      // Allow sret here and check the wrapped function in verifyStatepoint.
      if (!Call.getCalledFunction() ||
          Call.getCalledFunction()->getIntrinsicID() !=
              Intrinsic::experimental_gc_statepoint)
        Check(!ArgAttrs.hasAttribute(Attribute::StructRet),
              "Attribute 'sret' cannot be used for vararg call arguments!",
              Call);

      if (ArgAttrs.hasAttribute(Attribute::InAlloca))
        Check(Idx == Call.arg_size() - 1,
              "inalloca isn't on the last argument!", Call);
    }
  }

  // Verify that there's no metadata unless it's a direct call to an intrinsic.
  if (!IsIntrinsic) {
    for (Type *ParamTy : FTy->params()) {
      Check(!ParamTy->isMetadataTy(),
            "Function has metadata parameter but isn't an intrinsic", Call);
      Check(!ParamTy->isTokenTy(),
            "Function has token parameter but isn't an intrinsic", Call);
    }
  }

  // Verify that indirect calls don't return tokens.
  if (!Call.getCalledFunction()) {
    Check(!FTy->getReturnType()->isTokenTy(),
          "Return type cannot be token for indirect call!");
    Check(!FTy->getReturnType()->isX86_AMXTy(),
          "Return type cannot be x86_amx for indirect call!");
  }

  if (Function *F = Call.getCalledFunction())
    if (Intrinsic::ID ID = (Intrinsic::ID)F->getIntrinsicID())
      visitIntrinsicCall(ID, Call);

  // Verify that a callsite has at most one "deopt", at most one "funclet", at
  // most one "gc-transition", at most one "cfguardtarget", at most one
  // "preallocated" operand bundle, and at most one "ptrauth" operand bundle.
  bool FoundDeoptBundle = false, FoundFuncletBundle = false,
       FoundGCTransitionBundle = false, FoundCFGuardTargetBundle = false,
       FoundPreallocatedBundle = false, FoundGCLiveBundle = false,
       FoundPtrauthBundle = false, FoundKCFIBundle = false,
       FoundAttachedCallBundle = false;
  for (unsigned i = 0, e = Call.getNumOperandBundles(); i < e; ++i) {
    OperandBundleUse BU = Call.getOperandBundleAt(i);
    uint32_t Tag = BU.getTagID();
    if (Tag == LLVMContext::OB_deopt) {
      Check(!FoundDeoptBundle, "Multiple deopt operand bundles", Call);
      FoundDeoptBundle = true;
    } else if (Tag == LLVMContext::OB_gc_transition) {
      Check(!FoundGCTransitionBundle, "Multiple gc-transition operand bundles",
            Call);
      FoundGCTransitionBundle = true;
    } else if (Tag == LLVMContext::OB_funclet) {
      Check(!FoundFuncletBundle, "Multiple funclet operand bundles", Call);
      FoundFuncletBundle = true;
      Check(BU.Inputs.size() == 1,
            "Expected exactly one funclet bundle operand", Call);
      Check(isa<FuncletPadInst>(BU.Inputs.front()),
            "Funclet bundle operands should correspond to a FuncletPadInst",
            Call);
    } else if (Tag == LLVMContext::OB_cfguardtarget) {
      Check(!FoundCFGuardTargetBundle, "Multiple CFGuardTarget operand bundles",
            Call);
      FoundCFGuardTargetBundle = true;
      Check(BU.Inputs.size() == 1,
            "Expected exactly one cfguardtarget bundle operand", Call);
    } else if (Tag == LLVMContext::OB_ptrauth) {
      Check(!FoundPtrauthBundle, "Multiple ptrauth operand bundles", Call);
      FoundPtrauthBundle = true;
      Check(BU.Inputs.size() == 2,
            "Expected exactly two ptrauth bundle operands", Call);
      Check(isa<ConstantInt>(BU.Inputs[0]) &&
                BU.Inputs[0]->getType()->isIntegerTy(32),
            "Ptrauth bundle key operand must be an i32 constant", Call);
      Check(BU.Inputs[1]->getType()->isIntegerTy(64),
            "Ptrauth bundle discriminator operand must be an i64", Call);
    } else if (Tag == LLVMContext::OB_kcfi) {
      Check(!FoundKCFIBundle, "Multiple kcfi operand bundles", Call);
      FoundKCFIBundle = true;
      Check(BU.Inputs.size() == 1, "Expected exactly one kcfi bundle operand",
            Call);
      Check(isa<ConstantInt>(BU.Inputs[0]) &&
                BU.Inputs[0]->getType()->isIntegerTy(32),
            "Kcfi bundle operand must be an i32 constant", Call);
    } else if (Tag == LLVMContext::OB_preallocated) {
      Check(!FoundPreallocatedBundle, "Multiple preallocated operand bundles",
            Call);
      FoundPreallocatedBundle = true;
      Check(BU.Inputs.size() == 1,
            "Expected exactly one preallocated bundle operand", Call);
      auto Input = dyn_cast<IntrinsicInst>(BU.Inputs.front());
      Check(Input &&
                Input->getIntrinsicID() == Intrinsic::call_preallocated_setup,
            "\"preallocated\" argument must be a token from "
            "llvm.call.preallocated.setup",
            Call);
    } else if (Tag == LLVMContext::OB_gc_live) {
      Check(!FoundGCLiveBundle, "Multiple gc-live operand bundles", Call);
      FoundGCLiveBundle = true;
    } else if (Tag == LLVMContext::OB_clang_arc_attachedcall) {
      Check(!FoundAttachedCallBundle,
            "Multiple \"clang.arc.attachedcall\" operand bundles", Call);
      FoundAttachedCallBundle = true;
      verifyAttachedCallBundle(Call, BU);
    }
  }

  // Verify that callee and callsite agree on whether to use pointer auth.
  Check(!(Call.getCalledFunction() && FoundPtrauthBundle),
        "Direct call cannot have a ptrauth bundle", Call);

  // Verify that each inlinable callsite of a debug-info-bearing function in a
  // debug-info-bearing function has a debug location attached to it. Failure to
  // do so causes assertion failures when the inliner sets up inline scope info
  // (Interposable functions are not inlinable, neither are functions without
  //  definitions.)
  if (Call.getFunction()->getSubprogram() && Call.getCalledFunction() &&
      !Call.getCalledFunction()->isInterposable() &&
      !Call.getCalledFunction()->isDeclaration() &&
      Call.getCalledFunction()->getSubprogram())
    CheckDI(Call.getDebugLoc(),
            "inlinable function call in a function with "
            "debug info must have a !dbg location",
            Call);

  if (Call.isInlineAsm())
    verifyInlineAsmCall(Call);

  ConvergenceVerifyHelper.visit(Call);

  visitInstruction(Call);
}

void Verifier::verifyTailCCMustTailAttrs(const AttrBuilder &Attrs,
                                         StringRef Context) {
  Check(!Attrs.contains(Attribute::InAlloca),
        Twine("inalloca attribute not allowed in ") + Context);
  Check(!Attrs.contains(Attribute::InReg),
        Twine("inreg attribute not allowed in ") + Context);
  Check(!Attrs.contains(Attribute::SwiftError),
        Twine("swifterror attribute not allowed in ") + Context);
  Check(!Attrs.contains(Attribute::Preallocated),
        Twine("preallocated attribute not allowed in ") + Context);
  Check(!Attrs.contains(Attribute::ByRef),
        Twine("byref attribute not allowed in ") + Context);
}

/// Two types are "congruent" if they are identical, or if they are both pointer
/// types with different pointee types and the same address space.
static bool isTypeCongruent(Type *L, Type *R) {
  if (L == R)
    return true;
  PointerType *PL = dyn_cast<PointerType>(L);
  PointerType *PR = dyn_cast<PointerType>(R);
  if (!PL || !PR)
    return false;
  return PL->getAddressSpace() == PR->getAddressSpace();
}

static AttrBuilder getParameterABIAttributes(LLVMContext& C, unsigned I, AttributeList Attrs) {
  static const Attribute::AttrKind ABIAttrs[] = {
      Attribute::StructRet,  Attribute::ByVal,          Attribute::InAlloca,
      Attribute::InReg,      Attribute::StackAlignment, Attribute::SwiftSelf,
      Attribute::SwiftAsync, Attribute::SwiftError,     Attribute::Preallocated,
      Attribute::ByRef};
  AttrBuilder Copy(C);
  for (auto AK : ABIAttrs) {
    Attribute Attr = Attrs.getParamAttrs(I).getAttribute(AK);
    if (Attr.isValid())
      Copy.addAttribute(Attr);
  }

  // `align` is ABI-affecting only in combination with `byval` or `byref`.
  if (Attrs.hasParamAttr(I, Attribute::Alignment) &&
      (Attrs.hasParamAttr(I, Attribute::ByVal) ||
       Attrs.hasParamAttr(I, Attribute::ByRef)))
    Copy.addAlignmentAttr(Attrs.getParamAlignment(I));
  return Copy;
}

void Verifier::verifyMustTailCall(CallInst &CI) {
  Check(!CI.isInlineAsm(), "cannot use musttail call with inline asm", &CI);

  Function *F = CI.getParent()->getParent();
  FunctionType *CallerTy = F->getFunctionType();
  FunctionType *CalleeTy = CI.getFunctionType();
  Check(CallerTy->isVarArg() == CalleeTy->isVarArg(),
        "cannot guarantee tail call due to mismatched varargs", &CI);
  Check(isTypeCongruent(CallerTy->getReturnType(), CalleeTy->getReturnType()),
        "cannot guarantee tail call due to mismatched return types", &CI);

  // - The calling conventions of the caller and callee must match.
  Check(F->getCallingConv() == CI.getCallingConv(),
        "cannot guarantee tail call due to mismatched calling conv", &CI);

  // - The call must immediately precede a :ref:`ret <i_ret>` instruction,
  //   or a pointer bitcast followed by a ret instruction.
  // - The ret instruction must return the (possibly bitcasted) value
  //   produced by the call or void.
  Value *RetVal = &CI;
  Instruction *Next = CI.getNextNode();

  // Handle the optional bitcast.
  if (BitCastInst *BI = dyn_cast_or_null<BitCastInst>(Next)) {
    Check(BI->getOperand(0) == RetVal,
          "bitcast following musttail call must use the call", BI);
    RetVal = BI;
    Next = BI->getNextNode();
  }

  // Check the return.
  ReturnInst *Ret = dyn_cast_or_null<ReturnInst>(Next);
  Check(Ret, "musttail call must precede a ret with an optional bitcast", &CI);
  Check(!Ret->getReturnValue() || Ret->getReturnValue() == RetVal ||
            isa<UndefValue>(Ret->getReturnValue()),
        "musttail call result must be returned", Ret);

  AttributeList CallerAttrs = F->getAttributes();
  AttributeList CalleeAttrs = CI.getAttributes();
  if (CI.getCallingConv() == CallingConv::SwiftTail ||
      CI.getCallingConv() == CallingConv::Tail) {
    StringRef CCName =
        CI.getCallingConv() == CallingConv::Tail ? "tailcc" : "swifttailcc";

    // - Only sret, byval, swiftself, and swiftasync ABI-impacting attributes
    //   are allowed in swifttailcc call
    for (unsigned I = 0, E = CallerTy->getNumParams(); I != E; ++I) {
      AttrBuilder ABIAttrs = getParameterABIAttributes(F->getContext(), I, CallerAttrs);
      SmallString<32> Context{CCName, StringRef(" musttail caller")};
      verifyTailCCMustTailAttrs(ABIAttrs, Context);
    }
    for (unsigned I = 0, E = CalleeTy->getNumParams(); I != E; ++I) {
      AttrBuilder ABIAttrs = getParameterABIAttributes(F->getContext(), I, CalleeAttrs);
      SmallString<32> Context{CCName, StringRef(" musttail callee")};
      verifyTailCCMustTailAttrs(ABIAttrs, Context);
    }
    // - Varargs functions are not allowed
    Check(!CallerTy->isVarArg(), Twine("cannot guarantee ") + CCName +
                                     " tail call for varargs function");
    return;
  }

  // - The caller and callee prototypes must match.  Pointer types of
  //   parameters or return types may differ in pointee type, but not
  //   address space.
  if (!CI.getCalledFunction() || !CI.getCalledFunction()->isIntrinsic()) {
    Check(CallerTy->getNumParams() == CalleeTy->getNumParams(),
          "cannot guarantee tail call due to mismatched parameter counts", &CI);
    for (unsigned I = 0, E = CallerTy->getNumParams(); I != E; ++I) {
      Check(
          isTypeCongruent(CallerTy->getParamType(I), CalleeTy->getParamType(I)),
          "cannot guarantee tail call due to mismatched parameter types", &CI);
    }
  }

  // - All ABI-impacting function attributes, such as sret, byval, inreg,
  //   returned, preallocated, and inalloca, must match.
  for (unsigned I = 0, E = CallerTy->getNumParams(); I != E; ++I) {
    AttrBuilder CallerABIAttrs = getParameterABIAttributes(F->getContext(), I, CallerAttrs);
    AttrBuilder CalleeABIAttrs = getParameterABIAttributes(F->getContext(), I, CalleeAttrs);
    Check(CallerABIAttrs == CalleeABIAttrs,
          "cannot guarantee tail call due to mismatched ABI impacting "
          "function attributes",
          &CI, CI.getOperand(I));
  }
}

void Verifier::visitCallInst(CallInst &CI) {
  visitCallBase(CI);

  if (CI.isMustTailCall())
    verifyMustTailCall(CI);
}

void Verifier::visitInvokeInst(InvokeInst &II) {
  visitCallBase(II);

  // Verify that the first non-PHI instruction of the unwind destination is an
  // exception handling instruction.
  Check(
      II.getUnwindDest()->isEHPad(),
      "The unwind destination does not have an exception handling instruction!",
      &II);

  visitTerminator(II);
}

/// visitUnaryOperator - Check the argument to the unary operator.
///
void Verifier::visitUnaryOperator(UnaryOperator &U) {
  Check(U.getType() == U.getOperand(0)->getType(),
        "Unary operators must have same type for"
        "operands and result!",
        &U);

  switch (U.getOpcode()) {
  // Check that floating-point arithmetic operators are only used with
  // floating-point operands.
  case Instruction::FNeg:
    Check(U.getType()->isFPOrFPVectorTy(),
          "FNeg operator only works with float types!", &U);
    break;
  default:
    llvm_unreachable("Unknown UnaryOperator opcode!");
  }

  visitInstruction(U);
}

/// visitBinaryOperator - Check that both arguments to the binary operator are
/// of the same type!
///
void Verifier::visitBinaryOperator(BinaryOperator &B) {
  Check(B.getOperand(0)->getType() == B.getOperand(1)->getType(),
        "Both operands to a binary operator are not of the same type!", &B);

  switch (B.getOpcode()) {
  // Check that integer arithmetic operators are only used with
  // integral operands.
  case Instruction::Add:
  case Instruction::Sub:
  case Instruction::Mul:
  case Instruction::SDiv:
  case Instruction::UDiv:
  case Instruction::SRem:
  case Instruction::URem:
    Check(B.getType()->isIntOrIntVectorTy(),
          "Integer arithmetic operators only work with integral types!", &B);
    Check(B.getType() == B.getOperand(0)->getType(),
          "Integer arithmetic operators must have same type "
          "for operands and result!",
          &B);
    break;
  // Check that floating-point arithmetic operators are only used with
  // floating-point operands.
  case Instruction::FAdd:
  case Instruction::FSub:
  case Instruction::FMul:
  case Instruction::FDiv:
  case Instruction::FRem:
    Check(B.getType()->isFPOrFPVectorTy(),
          "Floating-point arithmetic operators only work with "
          "floating-point types!",
          &B);
    Check(B.getType() == B.getOperand(0)->getType(),
          "Floating-point arithmetic operators must have same type "
          "for operands and result!",
          &B);
    break;
  // Check that logical operators are only used with integral operands.
  case Instruction::And:
  case Instruction::Or:
  case Instruction::Xor:
    Check(B.getType()->isIntOrIntVectorTy(),
          "Logical operators only work with integral types!", &B);
    Check(B.getType() == B.getOperand(0)->getType(),
          "Logical operators must have same type for operands and result!", &B);
    break;
  case Instruction::Shl:
  case Instruction::LShr:
  case Instruction::AShr:
    Check(B.getType()->isIntOrIntVectorTy(),
          "Shifts only work with integral types!", &B);
    Check(B.getType() == B.getOperand(0)->getType(),
          "Shift return type must be same as operands!", &B);
    break;
  default:
    llvm_unreachable("Unknown BinaryOperator opcode!");
  }

  visitInstruction(B);
}

void Verifier::visitICmpInst(ICmpInst &IC) {
  // Check that the operands are the same type
  Type *Op0Ty = IC.getOperand(0)->getType();
  Type *Op1Ty = IC.getOperand(1)->getType();
  Check(Op0Ty == Op1Ty,
        "Both operands to ICmp instruction are not of the same type!", &IC);
  // Check that the operands are the right type
  Check(Op0Ty->isIntOrIntVectorTy() || Op0Ty->isPtrOrPtrVectorTy(),
        "Invalid operand types for ICmp instruction", &IC);
  // Check that the predicate is valid.
  Check(IC.isIntPredicate(), "Invalid predicate in ICmp instruction!", &IC);

  visitInstruction(IC);
}

void Verifier::visitFCmpInst(FCmpInst &FC) {
  // Check that the operands are the same type
  Type *Op0Ty = FC.getOperand(0)->getType();
  Type *Op1Ty = FC.getOperand(1)->getType();
  Check(Op0Ty == Op1Ty,
        "Both operands to FCmp instruction are not of the same type!", &FC);
  // Check that the operands are the right type
  Check(Op0Ty->isFPOrFPVectorTy(), "Invalid operand types for FCmp instruction",
        &FC);
  // Check that the predicate is valid.
  Check(FC.isFPPredicate(), "Invalid predicate in FCmp instruction!", &FC);

  visitInstruction(FC);
}

void Verifier::visitExtractElementInst(ExtractElementInst &EI) {
  Check(ExtractElementInst::isValidOperands(EI.getOperand(0), EI.getOperand(1)),
        "Invalid extractelement operands!", &EI);
  visitInstruction(EI);
}

void Verifier::visitInsertElementInst(InsertElementInst &IE) {
  Check(InsertElementInst::isValidOperands(IE.getOperand(0), IE.getOperand(1),
                                           IE.getOperand(2)),
        "Invalid insertelement operands!", &IE);
  visitInstruction(IE);
}

void Verifier::visitShuffleVectorInst(ShuffleVectorInst &SV) {
  Check(ShuffleVectorInst::isValidOperands(SV.getOperand(0), SV.getOperand(1),
                                           SV.getShuffleMask()),
        "Invalid shufflevector operands!", &SV);
  visitInstruction(SV);
}

void Verifier::visitGetElementPtrInst(GetElementPtrInst &GEP) {
  Type *TargetTy = GEP.getPointerOperandType()->getScalarType();

  Check(isa<PointerType>(TargetTy),
        "GEP base pointer is not a vector or a vector of pointers", &GEP);
  Check(GEP.getSourceElementType()->isSized(), "GEP into unsized type!", &GEP);

  if (auto *STy = dyn_cast<StructType>(GEP.getSourceElementType())) {
    SmallPtrSet<Type *, 4> Visited;
    Check(!STy->containsScalableVectorType(&Visited),
          "getelementptr cannot target structure that contains scalable vector"
          "type",
          &GEP);
  }

  SmallVector<Value *, 16> Idxs(GEP.indices());
  Check(
      all_of(Idxs, [](Value *V) { return V->getType()->isIntOrIntVectorTy(); }),
      "GEP indexes must be integers", &GEP);
  Type *ElTy =
      GetElementPtrInst::getIndexedType(GEP.getSourceElementType(), Idxs);
  Check(ElTy, "Invalid indices for GEP pointer type!", &GEP);

  Check(GEP.getType()->isPtrOrPtrVectorTy() &&
            GEP.getResultElementType() == ElTy,
        "GEP is not of right type for indices!", &GEP, ElTy);

  if (auto *GEPVTy = dyn_cast<VectorType>(GEP.getType())) {
    // Additional checks for vector GEPs.
    ElementCount GEPWidth = GEPVTy->getElementCount();
    if (GEP.getPointerOperandType()->isVectorTy())
      Check(
          GEPWidth ==
              cast<VectorType>(GEP.getPointerOperandType())->getElementCount(),
          "Vector GEP result width doesn't match operand's", &GEP);
    for (Value *Idx : Idxs) {
      Type *IndexTy = Idx->getType();
      if (auto *IndexVTy = dyn_cast<VectorType>(IndexTy)) {
        ElementCount IndexWidth = IndexVTy->getElementCount();
        Check(IndexWidth == GEPWidth, "Invalid GEP index vector width", &GEP);
      }
      Check(IndexTy->isIntOrIntVectorTy(),
            "All GEP indices should be of integer type");
    }
  }

  if (auto *PTy = dyn_cast<PointerType>(GEP.getType())) {
    Check(GEP.getAddressSpace() == PTy->getAddressSpace(),
          "GEP address space doesn't match type", &GEP);
  }

  visitInstruction(GEP);
}

static bool isContiguous(const ConstantRange &A, const ConstantRange &B) {
  return A.getUpper() == B.getLower() || A.getLower() == B.getUpper();
}

/// Verify !range and !absolute_symbol metadata. These have the same
/// restrictions, except !absolute_symbol allows the full set.
void Verifier::verifyRangeMetadata(const Value &I, const MDNode *Range,
                                   Type *Ty, bool IsAbsoluteSymbol) {
  unsigned NumOperands = Range->getNumOperands();
  Check(NumOperands % 2 == 0, "Unfinished range!", Range);
  unsigned NumRanges = NumOperands / 2;
  Check(NumRanges >= 1, "It should have at least one range!", Range);

  ConstantRange LastRange(1, true); // Dummy initial value
  for (unsigned i = 0; i < NumRanges; ++i) {
    ConstantInt *Low =
        mdconst::dyn_extract<ConstantInt>(Range->getOperand(2 * i));
    Check(Low, "The lower limit must be an integer!", Low);
    ConstantInt *High =
        mdconst::dyn_extract<ConstantInt>(Range->getOperand(2 * i + 1));
    Check(High, "The upper limit must be an integer!", High);
    Check(High->getType() == Low->getType() &&
          High->getType() == Ty->getScalarType(),
          "Range types must match instruction type!", &I);

    APInt HighV = High->getValue();
    APInt LowV = Low->getValue();

    // ConstantRange asserts if the ranges are the same except for the min/max
    // value. Leave the cases it tolerates for the empty range error below.
    Check(LowV != HighV || LowV.isMaxValue() || LowV.isMinValue(),
          "The upper and lower limits cannot be the same value", &I);

    ConstantRange CurRange(LowV, HighV);
    Check(!CurRange.isEmptySet() && (IsAbsoluteSymbol || !CurRange.isFullSet()),
          "Range must not be empty!", Range);
    if (i != 0) {
      Check(CurRange.intersectWith(LastRange).isEmptySet(),
            "Intervals are overlapping", Range);
      Check(LowV.sgt(LastRange.getLower()), "Intervals are not in order",
            Range);
      Check(!isContiguous(CurRange, LastRange), "Intervals are contiguous",
            Range);
    }
    LastRange = ConstantRange(LowV, HighV);
  }
  if (NumRanges > 2) {
    APInt FirstLow =
        mdconst::dyn_extract<ConstantInt>(Range->getOperand(0))->getValue();
    APInt FirstHigh =
        mdconst::dyn_extract<ConstantInt>(Range->getOperand(1))->getValue();
    ConstantRange FirstRange(FirstLow, FirstHigh);
    Check(FirstRange.intersectWith(LastRange).isEmptySet(),
          "Intervals are overlapping", Range);
    Check(!isContiguous(FirstRange, LastRange), "Intervals are contiguous",
          Range);
  }
}

void Verifier::visitRangeMetadata(Instruction &I, MDNode *Range, Type *Ty) {
  assert(Range && Range == I.getMetadata(LLVMContext::MD_range) &&
         "precondition violation");
  verifyRangeMetadata(I, Range, Ty, false);
}

void Verifier::checkAtomicMemAccessSize(Type *Ty, const Instruction *I) {
  unsigned Size = DL.getTypeSizeInBits(Ty);
  Check(Size >= 8, "atomic memory access' size must be byte-sized", Ty, I);
  Check(!(Size & (Size - 1)),
        "atomic memory access' operand must have a power-of-two size", Ty, I);
}

void Verifier::visitLoadInst(LoadInst &LI) {
  PointerType *PTy = dyn_cast<PointerType>(LI.getOperand(0)->getType());
  Check(PTy, "Load operand must be a pointer.", &LI);
  Type *ElTy = LI.getType();
  if (MaybeAlign A = LI.getAlign()) {
    Check(A->value() <= Value::MaximumAlignment,
          "huge alignment values are unsupported", &LI);
  }
  Check(ElTy->isSized(), "loading unsized types is not allowed", &LI);
  if (LI.isAtomic()) {
    Check(LI.getOrdering() != AtomicOrdering::Release &&
              LI.getOrdering() != AtomicOrdering::AcquireRelease,
          "Load cannot have Release ordering", &LI);
    Check(ElTy->isIntOrPtrTy() || ElTy->isFloatingPointTy(),
          "atomic load operand must have integer, pointer, or floating point "
          "type!",
          ElTy, &LI);
    checkAtomicMemAccessSize(ElTy, &LI);
  } else {
    Check(LI.getSyncScopeID() == SyncScope::System,
          "Non-atomic load cannot have SynchronizationScope specified", &LI);
  }

  visitInstruction(LI);
}

void Verifier::visitStoreInst(StoreInst &SI) {
  PointerType *PTy = dyn_cast<PointerType>(SI.getOperand(1)->getType());
  Check(PTy, "Store operand must be a pointer.", &SI);
  Type *ElTy = SI.getOperand(0)->getType();
  if (MaybeAlign A = SI.getAlign()) {
    Check(A->value() <= Value::MaximumAlignment,
          "huge alignment values are unsupported", &SI);
  }
  Check(ElTy->isSized(), "storing unsized types is not allowed", &SI);
  if (SI.isAtomic()) {
    Check(SI.getOrdering() != AtomicOrdering::Acquire &&
              SI.getOrdering() != AtomicOrdering::AcquireRelease,
          "Store cannot have Acquire ordering", &SI);
    Check(ElTy->isIntOrPtrTy() || ElTy->isFloatingPointTy(),
          "atomic store operand must have integer, pointer, or floating point "
          "type!",
          ElTy, &SI);
    checkAtomicMemAccessSize(ElTy, &SI);
  } else {
    Check(SI.getSyncScopeID() == SyncScope::System,
          "Non-atomic store cannot have SynchronizationScope specified", &SI);
  }
  visitInstruction(SI);
}

/// Check that SwiftErrorVal is used as a swifterror argument in CS.
void Verifier::verifySwiftErrorCall(CallBase &Call,
                                    const Value *SwiftErrorVal) {
  for (const auto &I : llvm::enumerate(Call.args())) {
    if (I.value() == SwiftErrorVal) {
      Check(Call.paramHasAttr(I.index(), Attribute::SwiftError),
            "swifterror value when used in a callsite should be marked "
            "with swifterror attribute",
            SwiftErrorVal, Call);
    }
  }
}

void Verifier::verifySwiftErrorValue(const Value *SwiftErrorVal) {
  // Check that swifterror value is only used by loads, stores, or as
  // a swifterror argument.
  for (const User *U : SwiftErrorVal->users()) {
    Check(isa<LoadInst>(U) || isa<StoreInst>(U) || isa<CallInst>(U) ||
              isa<InvokeInst>(U),
          "swifterror value can only be loaded and stored from, or "
          "as a swifterror argument!",
          SwiftErrorVal, U);
    // If it is used by a store, check it is the second operand.
    if (auto StoreI = dyn_cast<StoreInst>(U))
      Check(StoreI->getOperand(1) == SwiftErrorVal,
            "swifterror value should be the second operand when used "
            "by stores",
            SwiftErrorVal, U);
    if (auto *Call = dyn_cast<CallBase>(U))
      verifySwiftErrorCall(*const_cast<CallBase *>(Call), SwiftErrorVal);
  }
}

void Verifier::visitAllocaInst(AllocaInst &AI) {
  SmallPtrSet<Type*, 4> Visited;
  Check(AI.getAllocatedType()->isSized(&Visited),
        "Cannot allocate unsized type", &AI);
  Check(AI.getArraySize()->getType()->isIntegerTy(),
        "Alloca array size must have integer type", &AI);
  if (MaybeAlign A = AI.getAlign()) {
    Check(A->value() <= Value::MaximumAlignment,
          "huge alignment values are unsupported", &AI);
  }

  if (AI.isSwiftError()) {
    Check(AI.getAllocatedType()->isPointerTy(),
          "swifterror alloca must have pointer type", &AI);
    Check(!AI.isArrayAllocation(),
          "swifterror alloca must not be array allocation", &AI);
    verifySwiftErrorValue(&AI);
  }

  visitInstruction(AI);
}

void Verifier::visitAtomicCmpXchgInst(AtomicCmpXchgInst &CXI) {
  Type *ElTy = CXI.getOperand(1)->getType();
  Check(ElTy->isIntOrPtrTy(),
        "cmpxchg operand must have integer or pointer type", ElTy, &CXI);
  checkAtomicMemAccessSize(ElTy, &CXI);
  visitInstruction(CXI);
}

void Verifier::visitAtomicRMWInst(AtomicRMWInst &RMWI) {
  Check(RMWI.getOrdering() != AtomicOrdering::Unordered,
        "atomicrmw instructions cannot be unordered.", &RMWI);
  auto Op = RMWI.getOperation();
  Type *ElTy = RMWI.getOperand(1)->getType();
  if (Op == AtomicRMWInst::Xchg) {
    Check(ElTy->isIntegerTy() || ElTy->isFloatingPointTy() ||
              ElTy->isPointerTy(),
          "atomicrmw " + AtomicRMWInst::getOperationName(Op) +
              " operand must have integer or floating point type!",
          &RMWI, ElTy);
  } else if (AtomicRMWInst::isFPOperation(Op)) {
    Check(ElTy->isFPOrFPVectorTy() && !isa<ScalableVectorType>(ElTy),
          "atomicrmw " + AtomicRMWInst::getOperationName(Op) +
              " operand must have floating-point or fixed vector of floating-point "
              "type!",
          &RMWI, ElTy);
  } else {
    Check(ElTy->isIntegerTy(),
          "atomicrmw " + AtomicRMWInst::getOperationName(Op) +
              " operand must have integer type!",
          &RMWI, ElTy);
  }
  checkAtomicMemAccessSize(ElTy, &RMWI);
  Check(AtomicRMWInst::FIRST_BINOP <= Op && Op <= AtomicRMWInst::LAST_BINOP,
        "Invalid binary operation!", &RMWI);
  visitInstruction(RMWI);
}

void Verifier::visitFenceInst(FenceInst &FI) {
  const AtomicOrdering Ordering = FI.getOrdering();
  Check(Ordering == AtomicOrdering::Acquire ||
            Ordering == AtomicOrdering::Release ||
            Ordering == AtomicOrdering::AcquireRelease ||
            Ordering == AtomicOrdering::SequentiallyConsistent,
        "fence instructions may only have acquire, release, acq_rel, or "
        "seq_cst ordering.",
        &FI);
  visitInstruction(FI);
}

void Verifier::visitExtractValueInst(ExtractValueInst &EVI) {
  Check(ExtractValueInst::getIndexedType(EVI.getAggregateOperand()->getType(),
                                         EVI.getIndices()) == EVI.getType(),
        "Invalid ExtractValueInst operands!", &EVI);

  visitInstruction(EVI);
}

void Verifier::visitInsertValueInst(InsertValueInst &IVI) {
  Check(ExtractValueInst::getIndexedType(IVI.getAggregateOperand()->getType(),
                                         IVI.getIndices()) ==
            IVI.getOperand(1)->getType(),
        "Invalid InsertValueInst operands!", &IVI);

  visitInstruction(IVI);
}

static Value *getParentPad(Value *EHPad) {
  if (auto *FPI = dyn_cast<FuncletPadInst>(EHPad))
    return FPI->getParentPad();

  return cast<CatchSwitchInst>(EHPad)->getParentPad();
}

void Verifier::visitEHPadPredecessors(Instruction &I) {
  assert(I.isEHPad());

  BasicBlock *BB = I.getParent();
  Function *F = BB->getParent();

  Check(BB != &F->getEntryBlock(), "EH pad cannot be in entry block.", &I);

  if (auto *LPI = dyn_cast<LandingPadInst>(&I)) {
    // The landingpad instruction defines its parent as a landing pad block. The
    // landing pad block may be branched to only by the unwind edge of an
    // invoke.
    for (BasicBlock *PredBB : predecessors(BB)) {
      const auto *II = dyn_cast<InvokeInst>(PredBB->getTerminator());
      Check(II && II->getUnwindDest() == BB && II->getNormalDest() != BB,
            "Block containing LandingPadInst must be jumped to "
            "only by the unwind edge of an invoke.",
            LPI);
    }
    return;
  }
  if (auto *CPI = dyn_cast<CatchPadInst>(&I)) {
    if (!pred_empty(BB))
      Check(BB->getUniquePredecessor() == CPI->getCatchSwitch()->getParent(),
            "Block containg CatchPadInst must be jumped to "
            "only by its catchswitch.",
            CPI);
    Check(BB != CPI->getCatchSwitch()->getUnwindDest(),
          "Catchswitch cannot unwind to one of its catchpads",
          CPI->getCatchSwitch(), CPI);
    return;
  }

  // Verify that each pred has a legal terminator with a legal to/from EH
  // pad relationship.
  Instruction *ToPad = &I;
  Value *ToPadParent = getParentPad(ToPad);
  for (BasicBlock *PredBB : predecessors(BB)) {
    Instruction *TI = PredBB->getTerminator();
    Value *FromPad;
    if (auto *II = dyn_cast<InvokeInst>(TI)) {
      Check(II->getUnwindDest() == BB && II->getNormalDest() != BB,
            "EH pad must be jumped to via an unwind edge", ToPad, II);
      auto *CalledFn =
          dyn_cast<Function>(II->getCalledOperand()->stripPointerCasts());
      if (CalledFn && CalledFn->isIntrinsic() && II->doesNotThrow() &&
          !IntrinsicInst::mayLowerToFunctionCall(CalledFn->getIntrinsicID()))
        continue;
      if (auto Bundle = II->getOperandBundle(LLVMContext::OB_funclet))
        FromPad = Bundle->Inputs[0];
      else
        FromPad = ConstantTokenNone::get(II->getContext());
    } else if (auto *CRI = dyn_cast<CleanupReturnInst>(TI)) {
      FromPad = CRI->getOperand(0);
      Check(FromPad != ToPadParent, "A cleanupret must exit its cleanup", CRI);
    } else if (auto *CSI = dyn_cast<CatchSwitchInst>(TI)) {
      FromPad = CSI;
    } else {
      Check(false, "EH pad must be jumped to via an unwind edge", ToPad, TI);
    }

    // The edge may exit from zero or more nested pads.
    SmallSet<Value *, 8> Seen;
    for (;; FromPad = getParentPad(FromPad)) {
      Check(FromPad != ToPad,
            "EH pad cannot handle exceptions raised within it", FromPad, TI);
      if (FromPad == ToPadParent) {
        // This is a legal unwind edge.
        break;
      }
      Check(!isa<ConstantTokenNone>(FromPad),
            "A single unwind edge may only enter one EH pad", TI);
      Check(Seen.insert(FromPad).second, "EH pad jumps through a cycle of pads",
            FromPad);

      // This will be diagnosed on the corresponding instruction already. We
      // need the extra check here to make sure getParentPad() works.
      Check(isa<FuncletPadInst>(FromPad) || isa<CatchSwitchInst>(FromPad),
            "Parent pad must be catchpad/cleanuppad/catchswitch", TI);
    }
  }
}

void Verifier::visitLandingPadInst(LandingPadInst &LPI) {
  // The landingpad instruction is ill-formed if it doesn't have any clauses and
  // isn't a cleanup.
  Check(LPI.getNumClauses() > 0 || LPI.isCleanup(),
        "LandingPadInst needs at least one clause or to be a cleanup.", &LPI);

  visitEHPadPredecessors(LPI);

  if (!LandingPadResultTy)
    LandingPadResultTy = LPI.getType();
  else
    Check(LandingPadResultTy == LPI.getType(),
          "The landingpad instruction should have a consistent result type "
          "inside a function.",
          &LPI);

  Function *F = LPI.getParent()->getParent();
  Check(F->hasPersonalityFn(),
        "LandingPadInst needs to be in a function with a personality.", &LPI);

  // The landingpad instruction must be the first non-PHI instruction in the
  // block.
  Check(LPI.getParent()->getLandingPadInst() == &LPI,
        "LandingPadInst not the first non-PHI instruction in the block.", &LPI);

  for (unsigned i = 0, e = LPI.getNumClauses(); i < e; ++i) {
    Constant *Clause = LPI.getClause(i);
    if (LPI.isCatch(i)) {
      Check(isa<PointerType>(Clause->getType()),
            "Catch operand does not have pointer type!", &LPI);
    } else {
      Check(LPI.isFilter(i), "Clause is neither catch nor filter!", &LPI);
      Check(isa<ConstantArray>(Clause) || isa<ConstantAggregateZero>(Clause),
            "Filter operand is not an array of constants!", &LPI);
    }
  }

  visitInstruction(LPI);
}

void Verifier::visitResumeInst(ResumeInst &RI) {
  Check(RI.getFunction()->hasPersonalityFn(),
        "ResumeInst needs to be in a function with a personality.", &RI);

  if (!LandingPadResultTy)
    LandingPadResultTy = RI.getValue()->getType();
  else
    Check(LandingPadResultTy == RI.getValue()->getType(),
          "The resume instruction should have a consistent result type "
          "inside a function.",
          &RI);

  visitTerminator(RI);
}

void Verifier::visitCatchPadInst(CatchPadInst &CPI) {
  BasicBlock *BB = CPI.getParent();

  Function *F = BB->getParent();
  Check(F->hasPersonalityFn(),
        "CatchPadInst needs to be in a function with a personality.", &CPI);

  Check(isa<CatchSwitchInst>(CPI.getParentPad()),
        "CatchPadInst needs to be directly nested in a CatchSwitchInst.",
        CPI.getParentPad());

  // The catchpad instruction must be the first non-PHI instruction in the
  // block.
  Check(BB->getFirstNonPHI() == &CPI,
        "CatchPadInst not the first non-PHI instruction in the block.", &CPI);

  visitEHPadPredecessors(CPI);
  visitFuncletPadInst(CPI);
}

void Verifier::visitCatchReturnInst(CatchReturnInst &CatchReturn) {
  Check(isa<CatchPadInst>(CatchReturn.getOperand(0)),
        "CatchReturnInst needs to be provided a CatchPad", &CatchReturn,
        CatchReturn.getOperand(0));

  visitTerminator(CatchReturn);
}

void Verifier::visitCleanupPadInst(CleanupPadInst &CPI) {
  BasicBlock *BB = CPI.getParent();

  Function *F = BB->getParent();
  Check(F->hasPersonalityFn(),
        "CleanupPadInst needs to be in a function with a personality.", &CPI);

  // The cleanuppad instruction must be the first non-PHI instruction in the
  // block.
  Check(BB->getFirstNonPHI() == &CPI,
        "CleanupPadInst not the first non-PHI instruction in the block.", &CPI);

  auto *ParentPad = CPI.getParentPad();
  Check(isa<ConstantTokenNone>(ParentPad) || isa<FuncletPadInst>(ParentPad),
        "CleanupPadInst has an invalid parent.", &CPI);

  visitEHPadPredecessors(CPI);
  visitFuncletPadInst(CPI);
}

void Verifier::visitFuncletPadInst(FuncletPadInst &FPI) {
  User *FirstUser = nullptr;
  Value *FirstUnwindPad = nullptr;
  SmallVector<FuncletPadInst *, 8> Worklist({&FPI});
  SmallSet<FuncletPadInst *, 8> Seen;

  while (!Worklist.empty()) {
    FuncletPadInst *CurrentPad = Worklist.pop_back_val();
    Check(Seen.insert(CurrentPad).second,
          "FuncletPadInst must not be nested within itself", CurrentPad);
    Value *UnresolvedAncestorPad = nullptr;
    for (User *U : CurrentPad->users()) {
      BasicBlock *UnwindDest;
      if (auto *CRI = dyn_cast<CleanupReturnInst>(U)) {
        UnwindDest = CRI->getUnwindDest();
      } else if (auto *CSI = dyn_cast<CatchSwitchInst>(U)) {
        // We allow catchswitch unwind to caller to nest
        // within an outer pad that unwinds somewhere else,
        // because catchswitch doesn't have a nounwind variant.
        // See e.g. SimplifyCFGOpt::SimplifyUnreachable.
        if (CSI->unwindsToCaller())
          continue;
        UnwindDest = CSI->getUnwindDest();
      } else if (auto *II = dyn_cast<InvokeInst>(U)) {
        UnwindDest = II->getUnwindDest();
      } else if (isa<CallInst>(U)) {
        // Calls which don't unwind may be found inside funclet
        // pads that unwind somewhere else.  We don't *require*
        // such calls to be annotated nounwind.
        continue;
      } else if (auto *CPI = dyn_cast<CleanupPadInst>(U)) {
        // The unwind dest for a cleanup can only be found by
        // recursive search.  Add it to the worklist, and we'll
        // search for its first use that determines where it unwinds.
        Worklist.push_back(CPI);
        continue;
      } else {
        Check(isa<CatchReturnInst>(U), "Bogus funclet pad use", U);
        continue;
      }

      Value *UnwindPad;
      bool ExitsFPI;
      if (UnwindDest) {
        UnwindPad = UnwindDest->getFirstNonPHI();
        if (!cast<Instruction>(UnwindPad)->isEHPad())
          continue;
        Value *UnwindParent = getParentPad(UnwindPad);
        // Ignore unwind edges that don't exit CurrentPad.
        if (UnwindParent == CurrentPad)
          continue;
        // Determine whether the original funclet pad is exited,
        // and if we are scanning nested pads determine how many
        // of them are exited so we can stop searching their
        // children.
        Value *ExitedPad = CurrentPad;
        ExitsFPI = false;
        do {
          if (ExitedPad == &FPI) {
            ExitsFPI = true;
            // Now we can resolve any ancestors of CurrentPad up to
            // FPI, but not including FPI since we need to make sure
            // to check all direct users of FPI for consistency.
            UnresolvedAncestorPad = &FPI;
            break;
          }
          Value *ExitedParent = getParentPad(ExitedPad);
          if (ExitedParent == UnwindParent) {
            // ExitedPad is the ancestor-most pad which this unwind
            // edge exits, so we can resolve up to it, meaning that
            // ExitedParent is the first ancestor still unresolved.
            UnresolvedAncestorPad = ExitedParent;
            break;
          }
          ExitedPad = ExitedParent;
        } while (!isa<ConstantTokenNone>(ExitedPad));
      } else {
        // Unwinding to caller exits all pads.
        UnwindPad = ConstantTokenNone::get(FPI.getContext());
        ExitsFPI = true;
        UnresolvedAncestorPad = &FPI;
      }

      if (ExitsFPI) {
        // This unwind edge exits FPI.  Make sure it agrees with other
        // such edges.
        if (FirstUser) {
          Check(UnwindPad == FirstUnwindPad,
                "Unwind edges out of a funclet "
                "pad must have the same unwind "
                "dest",
                &FPI, U, FirstUser);
        } else {
          FirstUser = U;
          FirstUnwindPad = UnwindPad;
          // Record cleanup sibling unwinds for verifySiblingFuncletUnwinds
          if (isa<CleanupPadInst>(&FPI) && !isa<ConstantTokenNone>(UnwindPad) &&
              getParentPad(UnwindPad) == getParentPad(&FPI))
            SiblingFuncletInfo[&FPI] = cast<Instruction>(U);
        }
      }
      // Make sure we visit all uses of FPI, but for nested pads stop as
      // soon as we know where they unwind to.
      if (CurrentPad != &FPI)
        break;
    }
    if (UnresolvedAncestorPad) {
      if (CurrentPad == UnresolvedAncestorPad) {
        // When CurrentPad is FPI itself, we don't mark it as resolved even if
        // we've found an unwind edge that exits it, because we need to verify
        // all direct uses of FPI.
        assert(CurrentPad == &FPI);
        continue;
      }
      // Pop off the worklist any nested pads that we've found an unwind
      // destination for.  The pads on the worklist are the uncles,
      // great-uncles, etc. of CurrentPad.  We've found an unwind destination
      // for all ancestors of CurrentPad up to but not including
      // UnresolvedAncestorPad.
      Value *ResolvedPad = CurrentPad;
      while (!Worklist.empty()) {
        Value *UnclePad = Worklist.back();
        Value *AncestorPad = getParentPad(UnclePad);
        // Walk ResolvedPad up the ancestor list until we either find the
        // uncle's parent or the last resolved ancestor.
        while (ResolvedPad != AncestorPad) {
          Value *ResolvedParent = getParentPad(ResolvedPad);
          if (ResolvedParent == UnresolvedAncestorPad) {
            break;
          }
          ResolvedPad = ResolvedParent;
        }
        // If the resolved ancestor search didn't find the uncle's parent,
        // then the uncle is not yet resolved.
        if (ResolvedPad != AncestorPad)
          break;
        // This uncle is resolved, so pop it from the worklist.
        Worklist.pop_back();
      }
    }
  }

  if (FirstUnwindPad) {
    if (auto *CatchSwitch = dyn_cast<CatchSwitchInst>(FPI.getParentPad())) {
      BasicBlock *SwitchUnwindDest = CatchSwitch->getUnwindDest();
      Value *SwitchUnwindPad;
      if (SwitchUnwindDest)
        SwitchUnwindPad = SwitchUnwindDest->getFirstNonPHI();
      else
        SwitchUnwindPad = ConstantTokenNone::get(FPI.getContext());
      Check(SwitchUnwindPad == FirstUnwindPad,
            "Unwind edges out of a catch must have the same unwind dest as "
            "the parent catchswitch",
            &FPI, FirstUser, CatchSwitch);
    }
  }

  visitInstruction(FPI);
}

void Verifier::visitCatchSwitchInst(CatchSwitchInst &CatchSwitch) {
  BasicBlock *BB = CatchSwitch.getParent();

  Function *F = BB->getParent();
  Check(F->hasPersonalityFn(),
        "CatchSwitchInst needs to be in a function with a personality.",
        &CatchSwitch);

  // The catchswitch instruction must be the first non-PHI instruction in the
  // block.
  Check(BB->getFirstNonPHI() == &CatchSwitch,
        "CatchSwitchInst not the first non-PHI instruction in the block.",
        &CatchSwitch);

  auto *ParentPad = CatchSwitch.getParentPad();
  Check(isa<ConstantTokenNone>(ParentPad) || isa<FuncletPadInst>(ParentPad),
        "CatchSwitchInst has an invalid parent.", ParentPad);

  if (BasicBlock *UnwindDest = CatchSwitch.getUnwindDest()) {
    Instruction *I = UnwindDest->getFirstNonPHI();
    Check(I->isEHPad() && !isa<LandingPadInst>(I),
          "CatchSwitchInst must unwind to an EH block which is not a "
          "landingpad.",
          &CatchSwitch);

    // Record catchswitch sibling unwinds for verifySiblingFuncletUnwinds
    if (getParentPad(I) == ParentPad)
      SiblingFuncletInfo[&CatchSwitch] = &CatchSwitch;
  }

  Check(CatchSwitch.getNumHandlers() != 0,
        "CatchSwitchInst cannot have empty handler list", &CatchSwitch);

  for (BasicBlock *Handler : CatchSwitch.handlers()) {
    Check(isa<CatchPadInst>(Handler->getFirstNonPHI()),
          "CatchSwitchInst handlers must be catchpads", &CatchSwitch, Handler);
  }

  visitEHPadPredecessors(CatchSwitch);
  visitTerminator(CatchSwitch);
}

void Verifier::visitCleanupReturnInst(CleanupReturnInst &CRI) {
  Check(isa<CleanupPadInst>(CRI.getOperand(0)),
        "CleanupReturnInst needs to be provided a CleanupPad", &CRI,
        CRI.getOperand(0));

  if (BasicBlock *UnwindDest = CRI.getUnwindDest()) {
    Instruction *I = UnwindDest->getFirstNonPHI();
    Check(I->isEHPad() && !isa<LandingPadInst>(I),
          "CleanupReturnInst must unwind to an EH block which is not a "
          "landingpad.",
          &CRI);
  }

  visitTerminator(CRI);
}

void Verifier::verifyDominatesUse(Instruction &I, unsigned i) {
  Instruction *Op = cast<Instruction>(I.getOperand(i));
  // If the we have an invalid invoke, don't try to compute the dominance.
  // We already reject it in the invoke specific checks and the dominance
  // computation doesn't handle multiple edges.
  if (InvokeInst *II = dyn_cast<InvokeInst>(Op)) {
    if (II->getNormalDest() == II->getUnwindDest())
      return;
  }

  // Quick check whether the def has already been encountered in the same block.
  // PHI nodes are not checked to prevent accepting preceding PHIs, because PHI
  // uses are defined to happen on the incoming edge, not at the instruction.
  //
  // FIXME: If this operand is a MetadataAsValue (wrapping a LocalAsMetadata)
  // wrapping an SSA value, assert that we've already encountered it.  See
  // related FIXME in Mapper::mapLocalAsMetadata in ValueMapper.cpp.
  if (!isa<PHINode>(I) && InstsInThisBlock.count(Op))
    return;

  const Use &U = I.getOperandUse(i);
  Check(DT.dominates(Op, U), "Instruction does not dominate all uses!", Op, &I);
}

void Verifier::visitDereferenceableMetadata(Instruction& I, MDNode* MD) {
  Check(I.getType()->isPointerTy(),
        "dereferenceable, dereferenceable_or_null "
        "apply only to pointer types",
        &I);
  Check((isa<LoadInst>(I) || isa<IntToPtrInst>(I)),
        "dereferenceable, dereferenceable_or_null apply only to load"
        " and inttoptr instructions, use attributes for calls or invokes",
        &I);
  Check(MD->getNumOperands() == 1,
        "dereferenceable, dereferenceable_or_null "
        "take one operand!",
        &I);
  ConstantInt *CI = mdconst::dyn_extract<ConstantInt>(MD->getOperand(0));
  Check(CI && CI->getType()->isIntegerTy(64),
        "dereferenceable, "
        "dereferenceable_or_null metadata value must be an i64!",
        &I);
}

void Verifier::visitProfMetadata(Instruction &I, MDNode *MD) {
  Check(MD->getNumOperands() >= 2,
        "!prof annotations should have no less than 2 operands", MD);

  // Check first operand.
  Check(MD->getOperand(0) != nullptr, "first operand should not be null", MD);
  Check(isa<MDString>(MD->getOperand(0)),
        "expected string with name of the !prof annotation", MD);
  MDString *MDS = cast<MDString>(MD->getOperand(0));
  StringRef ProfName = MDS->getString();

  // Check consistency of !prof branch_weights metadata.
  if (ProfName == "branch_weights") {
    unsigned NumBranchWeights = getNumBranchWeights(*MD);
    if (isa<InvokeInst>(&I)) {
      Check(NumBranchWeights == 1 || NumBranchWeights == 2,
            "Wrong number of InvokeInst branch_weights operands", MD);
    } else {
      unsigned ExpectedNumOperands = 0;
      if (BranchInst *BI = dyn_cast<BranchInst>(&I))
        ExpectedNumOperands = BI->getNumSuccessors();
      else if (SwitchInst *SI = dyn_cast<SwitchInst>(&I))
        ExpectedNumOperands = SI->getNumSuccessors();
      else if (isa<CallInst>(&I))
        ExpectedNumOperands = 1;
      else if (IndirectBrInst *IBI = dyn_cast<IndirectBrInst>(&I))
        ExpectedNumOperands = IBI->getNumDestinations();
      else if (isa<SelectInst>(&I))
        ExpectedNumOperands = 2;
      else if (CallBrInst *CI = dyn_cast<CallBrInst>(&I))
        ExpectedNumOperands = CI->getNumSuccessors();
      else
        CheckFailed("!prof branch_weights are not allowed for this instruction",
                    MD);

      Check(NumBranchWeights == ExpectedNumOperands, "Wrong number of operands",
            MD);
    }
    for (unsigned i = getBranchWeightOffset(MD); i < MD->getNumOperands();
         ++i) {
      auto &MDO = MD->getOperand(i);
      Check(MDO, "second operand should not be null", MD);
      Check(mdconst::dyn_extract<ConstantInt>(MDO),
            "!prof brunch_weights operand is not a const int");
    }
  }
}

void Verifier::visitDIAssignIDMetadata(Instruction &I, MDNode *MD) {
  assert(I.hasMetadata(LLVMContext::MD_DIAssignID));
  bool ExpectedInstTy =
      isa<AllocaInst>(I) || isa<StoreInst>(I) || isa<MemIntrinsic>(I);
  CheckDI(ExpectedInstTy, "!DIAssignID attached to unexpected instruction kind",
          I, MD);
  // Iterate over the MetadataAsValue uses of the DIAssignID - these should
  // only be found as DbgAssignIntrinsic operands.
  if (auto *AsValue = MetadataAsValue::getIfExists(Context, MD)) {
    for (auto *User : AsValue->users()) {
      CheckDI(isa<DbgAssignIntrinsic>(User),
              "!DIAssignID should only be used by llvm.dbg.assign intrinsics",
              MD, User);
      // All of the dbg.assign intrinsics should be in the same function as I.
      if (auto *DAI = dyn_cast<DbgAssignIntrinsic>(User))
        CheckDI(DAI->getFunction() == I.getFunction(),
                "dbg.assign not in same function as inst", DAI, &I);
    }
  }
  for (DbgVariableRecord *DVR :
       cast<DIAssignID>(MD)->getAllDbgVariableRecordUsers()) {
    CheckDI(DVR->isDbgAssign(),
            "!DIAssignID should only be used by Assign DVRs.", MD, DVR);
    CheckDI(DVR->getFunction() == I.getFunction(),
            "DVRAssign not in same function as inst", DVR, &I);
  }
}

void Verifier::visitMMRAMetadata(Instruction &I, MDNode *MD) {
  Check(canInstructionHaveMMRAs(I),
        "!mmra metadata attached to unexpected instruction kind", I, MD);

  // MMRA Metadata should either be a tag, e.g. !{!"foo", !"bar"}, or a
  // list of tags such as !2 in the following example:
  //    !0 = !{!"a", !"b"}
  //    !1 = !{!"c", !"d"}
  //    !2 = !{!0, !1}
  if (MMRAMetadata::isTagMD(MD))
    return;

  Check(isa<MDTuple>(MD), "!mmra expected to be a metadata tuple", I, MD);
  for (const MDOperand &MDOp : MD->operands())
    Check(MMRAMetadata::isTagMD(MDOp.get()),
          "!mmra metadata tuple operand is not an MMRA tag", I, MDOp.get());
}

void Verifier::visitCallStackMetadata(MDNode *MD) {
  // Call stack metadata should consist of a list of at least 1 constant int
  // (representing a hash of the location).
  Check(MD->getNumOperands() >= 1,
        "call stack metadata should have at least 1 operand", MD);

  for (const auto &Op : MD->operands())
    Check(mdconst::dyn_extract_or_null<ConstantInt>(Op),
          "call stack metadata operand should be constant integer", Op);
}

void Verifier::visitMemProfMetadata(Instruction &I, MDNode *MD) {
  Check(isa<CallBase>(I), "!memprof metadata should only exist on calls", &I);
  Check(MD->getNumOperands() >= 1,
        "!memprof annotations should have at least 1 metadata operand "
        "(MemInfoBlock)",
        MD);

  // Check each MIB
  for (auto &MIBOp : MD->operands()) {
    MDNode *MIB = dyn_cast<MDNode>(MIBOp);
    // The first operand of an MIB should be the call stack metadata.
    // There rest of the operands should be MDString tags, and there should be
    // at least one.
    Check(MIB->getNumOperands() >= 2,
          "Each !memprof MemInfoBlock should have at least 2 operands", MIB);

    // Check call stack metadata (first operand).
    Check(MIB->getOperand(0) != nullptr,
          "!memprof MemInfoBlock first operand should not be null", MIB);
    Check(isa<MDNode>(MIB->getOperand(0)),
          "!memprof MemInfoBlock first operand should be an MDNode", MIB);
    MDNode *StackMD = dyn_cast<MDNode>(MIB->getOperand(0));
    visitCallStackMetadata(StackMD);

    // Check that remaining operands, except possibly the last, are MDString.
    Check(llvm::all_of(MIB->operands().drop_front().drop_back(),
                       [](const MDOperand &Op) { return isa<MDString>(Op); }),
          "Not all !memprof MemInfoBlock operands 1 to N-1 are MDString", MIB);
    // The last operand might be the total profiled size so can be an integer.
    auto &LastOperand = MIB->operands().back();
    Check(isa<MDString>(LastOperand) || mdconst::hasa<ConstantInt>(LastOperand),
          "Last !memprof MemInfoBlock operand not MDString or int", MIB);
  }
}

void Verifier::visitCallsiteMetadata(Instruction &I, MDNode *MD) {
  Check(isa<CallBase>(I), "!callsite metadata should only exist on calls", &I);
  // Verify the partial callstack annotated from memprof profiles. This callsite
  // is a part of a profiled allocation callstack.
  visitCallStackMetadata(MD);
}

void Verifier::visitAnnotationMetadata(MDNode *Annotation) {
  Check(isa<MDTuple>(Annotation), "annotation must be a tuple");
  Check(Annotation->getNumOperands() >= 1,
        "annotation must have at least one operand");
  for (const MDOperand &Op : Annotation->operands()) {
    bool TupleOfStrings =
        isa<MDTuple>(Op.get()) &&
        all_of(cast<MDTuple>(Op)->operands(), [](auto &Annotation) {
          return isa<MDString>(Annotation.get());
        });
    Check(isa<MDString>(Op.get()) || TupleOfStrings,
          "operands must be a string or a tuple of strings");
  }
}

void Verifier::visitAliasScopeMetadata(const MDNode *MD) {
  unsigned NumOps = MD->getNumOperands();
  Check(NumOps >= 2 && NumOps <= 3, "scope must have two or three operands",
        MD);
  Check(MD->getOperand(0).get() == MD || isa<MDString>(MD->getOperand(0)),
        "first scope operand must be self-referential or string", MD);
  if (NumOps == 3)
    Check(isa<MDString>(MD->getOperand(2)),
          "third scope operand must be string (if used)", MD);

  MDNode *Domain = dyn_cast<MDNode>(MD->getOperand(1));
  Check(Domain != nullptr, "second scope operand must be MDNode", MD);

  unsigned NumDomainOps = Domain->getNumOperands();
  Check(NumDomainOps >= 1 && NumDomainOps <= 2,
        "domain must have one or two operands", Domain);
  Check(Domain->getOperand(0).get() == Domain ||
            isa<MDString>(Domain->getOperand(0)),
        "first domain operand must be self-referential or string", Domain);
  if (NumDomainOps == 2)
    Check(isa<MDString>(Domain->getOperand(1)),
          "second domain operand must be string (if used)", Domain);
}

void Verifier::visitAliasScopeListMetadata(const MDNode *MD) {
  for (const MDOperand &Op : MD->operands()) {
    const MDNode *OpMD = dyn_cast<MDNode>(Op);
    Check(OpMD != nullptr, "scope list must consist of MDNodes", MD);
    visitAliasScopeMetadata(OpMD);
  }
}

void Verifier::visitAccessGroupMetadata(const MDNode *MD) {
  auto IsValidAccessScope = [](const MDNode *MD) {
    return MD->getNumOperands() == 0 && MD->isDistinct();
  };

  // It must be either an access scope itself...
  if (IsValidAccessScope(MD))
    return;

  // ...or a list of access scopes.
  for (const MDOperand &Op : MD->operands()) {
    const MDNode *OpMD = dyn_cast<MDNode>(Op);
    Check(OpMD != nullptr, "Access scope list must consist of MDNodes", MD);
    Check(IsValidAccessScope(OpMD),
          "Access scope list contains invalid access scope", MD);
  }
}

/// verifyInstruction - Verify that an instruction is well formed.
///
void Verifier::visitInstruction(Instruction &I) {
  BasicBlock *BB = I.getParent();
  Check(BB, "Instruction not embedded in basic block!", &I);

  if (!isa<PHINode>(I)) {   // Check that non-phi nodes are not self referential
    for (User *U : I.users()) {
      Check(U != (User *)&I || !DT.isReachableFromEntry(BB),
            "Only PHI nodes may reference their own value!", &I);
    }
  }

  // Check that void typed values don't have names
  Check(!I.getType()->isVoidTy() || !I.hasName(),
        "Instruction has a name, but provides a void value!", &I);

  // Check that the return value of the instruction is either void or a legal
  // value type.
  Check(I.getType()->isVoidTy() || I.getType()->isFirstClassType(),
        "Instruction returns a non-scalar type!", &I);

  // Check that the instruction doesn't produce metadata. Calls are already
  // checked against the callee type.
  Check(!I.getType()->isMetadataTy() || isa<CallInst>(I) || isa<InvokeInst>(I),
        "Invalid use of metadata!", &I);

  // Check that all uses of the instruction, if they are instructions
  // themselves, actually have parent basic blocks.  If the use is not an
  // instruction, it is an error!
  for (Use &U : I.uses()) {
    if (Instruction *Used = dyn_cast<Instruction>(U.getUser()))
      Check(Used->getParent() != nullptr,
            "Instruction referencing"
            " instruction not embedded in a basic block!",
            &I, Used);
    else {
      CheckFailed("Use of instruction is not an instruction!", U);
      return;
    }
  }

  // Get a pointer to the call base of the instruction if it is some form of
  // call.
  const CallBase *CBI = dyn_cast<CallBase>(&I);

  for (unsigned i = 0, e = I.getNumOperands(); i != e; ++i) {
    Check(I.getOperand(i) != nullptr, "Instruction has null operand!", &I);

    // Check to make sure that only first-class-values are operands to
    // instructions.
    if (!I.getOperand(i)->getType()->isFirstClassType()) {
      Check(false, "Instruction operands must be first-class values!", &I);
    }

    if (Function *F = dyn_cast<Function>(I.getOperand(i))) {
      // This code checks whether the function is used as the operand of a
      // clang_arc_attachedcall operand bundle.
      auto IsAttachedCallOperand = [](Function *F, const CallBase *CBI,
                                      int Idx) {
        return CBI && CBI->isOperandBundleOfType(
                          LLVMContext::OB_clang_arc_attachedcall, Idx);
      };

      // Check to make sure that the "address of" an intrinsic function is never
      // taken. Ignore cases where the address of the intrinsic function is used
      // as the argument of operand bundle "clang.arc.attachedcall" as those
      // cases are handled in verifyAttachedCallBundle.
      Check((!F->isIntrinsic() ||
             (CBI && &CBI->getCalledOperandUse() == &I.getOperandUse(i)) ||
             IsAttachedCallOperand(F, CBI, i)),
            "Cannot take the address of an intrinsic!", &I);
      Check(!F->isIntrinsic() || isa<CallInst>(I) ||
                F->getIntrinsicID() == Intrinsic::donothing ||
                F->getIntrinsicID() == Intrinsic::seh_try_begin ||
                F->getIntrinsicID() == Intrinsic::seh_try_end ||
                F->getIntrinsicID() == Intrinsic::seh_scope_begin ||
                F->getIntrinsicID() == Intrinsic::seh_scope_end ||
                F->getIntrinsicID() == Intrinsic::coro_resume ||
                F->getIntrinsicID() == Intrinsic::coro_destroy ||
                F->getIntrinsicID() == Intrinsic::coro_await_suspend_void ||
                F->getIntrinsicID() == Intrinsic::coro_await_suspend_bool ||
                F->getIntrinsicID() == Intrinsic::coro_await_suspend_handle ||
                F->getIntrinsicID() ==
                    Intrinsic::experimental_patchpoint_void ||
                F->getIntrinsicID() == Intrinsic::experimental_patchpoint ||
                F->getIntrinsicID() == Intrinsic::experimental_gc_statepoint ||
                F->getIntrinsicID() == Intrinsic::wasm_rethrow ||
                IsAttachedCallOperand(F, CBI, i),
            "Cannot invoke an intrinsic other than donothing, patchpoint, "
            "statepoint, coro_resume, coro_destroy or clang.arc.attachedcall",
            &I);
      Check(F->getParent() == &M, "Referencing function in another module!", &I,
            &M, F, F->getParent());
    } else if (BasicBlock *OpBB = dyn_cast<BasicBlock>(I.getOperand(i))) {
      Check(OpBB->getParent() == BB->getParent(),
            "Referring to a basic block in another function!", &I);
    } else if (Argument *OpArg = dyn_cast<Argument>(I.getOperand(i))) {
      Check(OpArg->getParent() == BB->getParent(),
            "Referring to an argument in another function!", &I);
    } else if (GlobalValue *GV = dyn_cast<GlobalValue>(I.getOperand(i))) {
      Check(GV->getParent() == &M, "Referencing global in another module!", &I,
            &M, GV, GV->getParent());
    } else if (Instruction *OpInst = dyn_cast<Instruction>(I.getOperand(i))) {
      Check(OpInst->getFunction() == BB->getParent(),
            "Referring to an instruction in another function!", &I);
      verifyDominatesUse(I, i);
    } else if (isa<InlineAsm>(I.getOperand(i))) {
      Check(CBI && &CBI->getCalledOperandUse() == &I.getOperandUse(i),
            "Cannot take the address of an inline asm!", &I);
    } else if (auto *CPA = dyn_cast<ConstantPtrAuth>(I.getOperand(i))) {
      visitConstantExprsRecursively(CPA);
    } else if (ConstantExpr *CE = dyn_cast<ConstantExpr>(I.getOperand(i))) {
      if (CE->getType()->isPtrOrPtrVectorTy()) {
        // If we have a ConstantExpr pointer, we need to see if it came from an
        // illegal bitcast.
        visitConstantExprsRecursively(CE);
      }
    }
  }

  if (MDNode *MD = I.getMetadata(LLVMContext::MD_fpmath)) {
    Check(I.getType()->isFPOrFPVectorTy(),
          "fpmath requires a floating point result!", &I);
    Check(MD->getNumOperands() == 1, "fpmath takes one operand!", &I);
    if (ConstantFP *CFP0 =
            mdconst::dyn_extract_or_null<ConstantFP>(MD->getOperand(0))) {
      const APFloat &Accuracy = CFP0->getValueAPF();
      Check(&Accuracy.getSemantics() == &APFloat::IEEEsingle(),
            "fpmath accuracy must have float type", &I);
      Check(Accuracy.isFiniteNonZero() && !Accuracy.isNegative(),
            "fpmath accuracy not a positive number!", &I);
    } else {
      Check(false, "invalid fpmath accuracy!", &I);
    }
  }

  if (MDNode *Range = I.getMetadata(LLVMContext::MD_range)) {
    Check(isa<LoadInst>(I) || isa<CallInst>(I) || isa<InvokeInst>(I),
          "Ranges are only for loads, calls and invokes!", &I);
    visitRangeMetadata(I, Range, I.getType());
  }

  if (I.hasMetadata(LLVMContext::MD_invariant_group)) {
    Check(isa<LoadInst>(I) || isa<StoreInst>(I),
          "invariant.group metadata is only for loads and stores", &I);
  }

  if (MDNode *MD = I.getMetadata(LLVMContext::MD_nonnull)) {
    Check(I.getType()->isPointerTy(), "nonnull applies only to pointer types",
          &I);
    Check(isa<LoadInst>(I),
          "nonnull applies only to load instructions, use attributes"
          " for calls or invokes",
          &I);
    Check(MD->getNumOperands() == 0, "nonnull metadata must be empty", &I);
  }

  if (MDNode *MD = I.getMetadata(LLVMContext::MD_dereferenceable))
    visitDereferenceableMetadata(I, MD);

  if (MDNode *MD = I.getMetadata(LLVMContext::MD_dereferenceable_or_null))
    visitDereferenceableMetadata(I, MD);

  if (MDNode *TBAA = I.getMetadata(LLVMContext::MD_tbaa))
    TBAAVerifyHelper.visitTBAAMetadata(I, TBAA);

  if (MDNode *MD = I.getMetadata(LLVMContext::MD_noalias))
    visitAliasScopeListMetadata(MD);
  if (MDNode *MD = I.getMetadata(LLVMContext::MD_alias_scope))
    visitAliasScopeListMetadata(MD);

  if (MDNode *MD = I.getMetadata(LLVMContext::MD_access_group))
    visitAccessGroupMetadata(MD);

  if (MDNode *AlignMD = I.getMetadata(LLVMContext::MD_align)) {
    Check(I.getType()->isPointerTy(), "align applies only to pointer types",
          &I);
    Check(isa<LoadInst>(I),
          "align applies only to load instructions, "
          "use attributes for calls or invokes",
          &I);
    Check(AlignMD->getNumOperands() == 1, "align takes one operand!", &I);
    ConstantInt *CI = mdconst::dyn_extract<ConstantInt>(AlignMD->getOperand(0));
    Check(CI && CI->getType()->isIntegerTy(64),
          "align metadata value must be an i64!", &I);
    uint64_t Align = CI->getZExtValue();
    Check(isPowerOf2_64(Align), "align metadata value must be a power of 2!",
          &I);
    Check(Align <= Value::MaximumAlignment,
          "alignment is larger that implementation defined limit", &I);
  }

  if (MDNode *MD = I.getMetadata(LLVMContext::MD_prof))
    visitProfMetadata(I, MD);

  if (MDNode *MD = I.getMetadata(LLVMContext::MD_memprof))
    visitMemProfMetadata(I, MD);

  if (MDNode *MD = I.getMetadata(LLVMContext::MD_callsite))
    visitCallsiteMetadata(I, MD);

  if (MDNode *MD = I.getMetadata(LLVMContext::MD_DIAssignID))
    visitDIAssignIDMetadata(I, MD);

  if (MDNode *MMRA = I.getMetadata(LLVMContext::MD_mmra))
    visitMMRAMetadata(I, MMRA);

  if (MDNode *Annotation = I.getMetadata(LLVMContext::MD_annotation))
    visitAnnotationMetadata(Annotation);

  if (MDNode *N = I.getDebugLoc().getAsMDNode()) {
    CheckDI(isa<DILocation>(N), "invalid !dbg metadata attachment", &I, N);
    visitMDNode(*N, AreDebugLocsAllowed::Yes);
  }

  if (auto *DII = dyn_cast<DbgVariableIntrinsic>(&I)) {
    verifyFragmentExpression(*DII);
    verifyNotEntryValue(*DII);
  }

  SmallVector<std::pair<unsigned, MDNode *>, 4> MDs;
  I.getAllMetadata(MDs);
  for (auto Attachment : MDs) {
    unsigned Kind = Attachment.first;
    auto AllowLocs =
        (Kind == LLVMContext::MD_dbg || Kind == LLVMContext::MD_loop)
            ? AreDebugLocsAllowed::Yes
            : AreDebugLocsAllowed::No;
    visitMDNode(*Attachment.second, AllowLocs);
  }

  InstsInThisBlock.insert(&I);
}

/// Allow intrinsics to be verified in different ways.
void Verifier::visitIntrinsicCall(Intrinsic::ID ID, CallBase &Call) {
  Function *IF = Call.getCalledFunction();
  Check(IF->isDeclaration(), "Intrinsic functions should never be defined!",
        IF);

  // Verify that the intrinsic prototype lines up with what the .td files
  // describe.
  FunctionType *IFTy = IF->getFunctionType();
  bool IsVarArg = IFTy->isVarArg();

  SmallVector<Intrinsic::IITDescriptor, 8> Table;
  getIntrinsicInfoTableEntries(ID, Table);
  ArrayRef<Intrinsic::IITDescriptor> TableRef = Table;

  // Walk the descriptors to extract overloaded types.
  SmallVector<Type *, 4> ArgTys;
  Intrinsic::MatchIntrinsicTypesResult Res =
      Intrinsic::matchIntrinsicSignature(IFTy, TableRef, ArgTys);
  Check(Res != Intrinsic::MatchIntrinsicTypes_NoMatchRet,
        "Intrinsic has incorrect return type!", IF);
  Check(Res != Intrinsic::MatchIntrinsicTypes_NoMatchArg,
        "Intrinsic has incorrect argument type!", IF);

  // Verify if the intrinsic call matches the vararg property.
  if (IsVarArg)
    Check(!Intrinsic::matchIntrinsicVarArg(IsVarArg, TableRef),
          "Intrinsic was not defined with variable arguments!", IF);
  else
    Check(!Intrinsic::matchIntrinsicVarArg(IsVarArg, TableRef),
          "Callsite was not defined with variable arguments!", IF);

  // All descriptors should be absorbed by now.
  Check(TableRef.empty(), "Intrinsic has too few arguments!", IF);

  // Now that we have the intrinsic ID and the actual argument types (and we
  // know they are legal for the intrinsic!) get the intrinsic name through the
  // usual means.  This allows us to verify the mangling of argument types into
  // the name.
  const std::string ExpectedName =
      Intrinsic::getName(ID, ArgTys, IF->getParent(), IFTy);
  Check(ExpectedName == IF->getName(),
        "Intrinsic name not mangled correctly for type arguments! "
        "Should be: " +
            ExpectedName,
        IF);

  // If the intrinsic takes MDNode arguments, verify that they are either global
  // or are local to *this* function.
  for (Value *V : Call.args()) {
    if (auto *MD = dyn_cast<MetadataAsValue>(V))
      visitMetadataAsValue(*MD, Call.getCaller());
    if (auto *Const = dyn_cast<Constant>(V))
      Check(!Const->getType()->isX86_AMXTy(),
            "const x86_amx is not allowed in argument!");
  }

  switch (ID) {
  default:
    break;
  case Intrinsic::assume: {
    for (auto &Elem : Call.bundle_op_infos()) {
      unsigned ArgCount = Elem.End - Elem.Begin;
      // Separate storage assumptions are special insofar as they're the only
      // operand bundles allowed on assumes that aren't parameter attributes.
      if (Elem.Tag->getKey() == "separate_storage") {
        Check(ArgCount == 2,
              "separate_storage assumptions should have 2 arguments", Call);
        Check(Call.getOperand(Elem.Begin)->getType()->isPointerTy() &&
                  Call.getOperand(Elem.Begin + 1)->getType()->isPointerTy(),
              "arguments to separate_storage assumptions should be pointers",
              Call);
        return;
      }
      Check(Elem.Tag->getKey() == "ignore" ||
                Attribute::isExistingAttribute(Elem.Tag->getKey()),
            "tags must be valid attribute names", Call);
      Attribute::AttrKind Kind =
          Attribute::getAttrKindFromName(Elem.Tag->getKey());
      if (Kind == Attribute::Alignment) {
        Check(ArgCount <= 3 && ArgCount >= 2,
              "alignment assumptions should have 2 or 3 arguments", Call);
        Check(Call.getOperand(Elem.Begin)->getType()->isPointerTy(),
              "first argument should be a pointer", Call);
        Check(Call.getOperand(Elem.Begin + 1)->getType()->isIntegerTy(),
              "second argument should be an integer", Call);
        if (ArgCount == 3)
          Check(Call.getOperand(Elem.Begin + 2)->getType()->isIntegerTy(),
                "third argument should be an integer if present", Call);
        return;
      }
      Check(ArgCount <= 2, "too many arguments", Call);
      if (Kind == Attribute::None)
        break;
      if (Attribute::isIntAttrKind(Kind)) {
        Check(ArgCount == 2, "this attribute should have 2 arguments", Call);
        Check(isa<ConstantInt>(Call.getOperand(Elem.Begin + 1)),
              "the second argument should be a constant integral value", Call);
      } else if (Attribute::canUseAsParamAttr(Kind)) {
        Check((ArgCount) == 1, "this attribute should have one argument", Call);
      } else if (Attribute::canUseAsFnAttr(Kind)) {
        Check((ArgCount) == 0, "this attribute has no argument", Call);
      }
    }
    break;
  }
  case Intrinsic::ucmp:
  case Intrinsic::scmp: {
    Type *SrcTy = Call.getOperand(0)->getType();
    Type *DestTy = Call.getType();

    Check(DestTy->getScalarSizeInBits() >= 2,
          "result type must be at least 2 bits wide", Call);

    bool IsDestTypeVector = DestTy->isVectorTy();
    Check(SrcTy->isVectorTy() == IsDestTypeVector,
          "ucmp/scmp argument and result types must both be either vector or "
          "scalar types",
          Call);
    if (IsDestTypeVector) {
      auto SrcVecLen = cast<VectorType>(SrcTy)->getElementCount();
      auto DestVecLen = cast<VectorType>(DestTy)->getElementCount();
      Check(SrcVecLen == DestVecLen,
            "return type and arguments must have the same number of "
            "elements",
            Call);
    }
    break;
  }
  case Intrinsic::coro_id: {
    auto *InfoArg = Call.getArgOperand(3)->stripPointerCasts();
    if (isa<ConstantPointerNull>(InfoArg))
      break;
    auto *GV = dyn_cast<GlobalVariable>(InfoArg);
    Check(GV && GV->isConstant() && GV->hasDefinitiveInitializer(),
          "info argument of llvm.coro.id must refer to an initialized "
          "constant");
    Constant *Init = GV->getInitializer();
    Check(isa<ConstantStruct>(Init) || isa<ConstantArray>(Init),
          "info argument of llvm.coro.id must refer to either a struct or "
          "an array");
    break;
  }
  case Intrinsic::is_fpclass: {
    const ConstantInt *TestMask = cast<ConstantInt>(Call.getOperand(1));
    Check((TestMask->getZExtValue() & ~static_cast<unsigned>(fcAllFlags)) == 0,
          "unsupported bits for llvm.is.fpclass test mask");
    break;
  }
  case Intrinsic::fptrunc_round: {
    // Check the rounding mode
    Metadata *MD = nullptr;
    auto *MAV = dyn_cast<MetadataAsValue>(Call.getOperand(1));
    if (MAV)
      MD = MAV->getMetadata();

    Check(MD != nullptr, "missing rounding mode argument", Call);

    Check(isa<MDString>(MD),
          ("invalid value for llvm.fptrunc.round metadata operand"
           " (the operand should be a string)"),
          MD);

    std::optional<RoundingMode> RoundMode =
        convertStrToRoundingMode(cast<MDString>(MD)->getString());
    Check(RoundMode && *RoundMode != RoundingMode::Dynamic,
          "unsupported rounding mode argument", Call);
    break;
  }
#define BEGIN_REGISTER_VP_INTRINSIC(VPID, ...) case Intrinsic::VPID:
#include "llvm/IR/VPIntrinsics.def"
#undef BEGIN_REGISTER_VP_INTRINSIC
    visitVPIntrinsic(cast<VPIntrinsic>(Call));
    break;
#define INSTRUCTION(NAME, NARGS, ROUND_MODE, INTRINSIC)                        \
  case Intrinsic::INTRINSIC:
#include "llvm/IR/ConstrainedOps.def"
#undef INSTRUCTION
    visitConstrainedFPIntrinsic(cast<ConstrainedFPIntrinsic>(Call));
    break;
  case Intrinsic::dbg_declare: // llvm.dbg.declare
    Check(isa<MetadataAsValue>(Call.getArgOperand(0)),
          "invalid llvm.dbg.declare intrinsic call 1", Call);
    visitDbgIntrinsic("declare", cast<DbgVariableIntrinsic>(Call));
    break;
  case Intrinsic::dbg_value: // llvm.dbg.value
    visitDbgIntrinsic("value", cast<DbgVariableIntrinsic>(Call));
    break;
  case Intrinsic::dbg_assign: // llvm.dbg.assign
    visitDbgIntrinsic("assign", cast<DbgVariableIntrinsic>(Call));
    break;
  case Intrinsic::dbg_label: // llvm.dbg.label
    visitDbgLabelIntrinsic("label", cast<DbgLabelInst>(Call));
    break;
  case Intrinsic::memcpy:
  case Intrinsic::memcpy_inline:
  case Intrinsic::memmove:
  case Intrinsic::memset:
  case Intrinsic::memset_inline: {
    break;
  }
  case Intrinsic::memcpy_element_unordered_atomic:
  case Intrinsic::memmove_element_unordered_atomic:
  case Intrinsic::memset_element_unordered_atomic: {
    const auto *AMI = cast<AtomicMemIntrinsic>(&Call);

    ConstantInt *ElementSizeCI =
        cast<ConstantInt>(AMI->getRawElementSizeInBytes());
    const APInt &ElementSizeVal = ElementSizeCI->getValue();
    Check(ElementSizeVal.isPowerOf2(),
          "element size of the element-wise atomic memory intrinsic "
          "must be a power of 2",
          Call);

    auto IsValidAlignment = [&](MaybeAlign Alignment) {
      return Alignment && ElementSizeVal.ule(Alignment->value());
    };
    Check(IsValidAlignment(AMI->getDestAlign()),
          "incorrect alignment of the destination argument", Call);
    if (const auto *AMT = dyn_cast<AtomicMemTransferInst>(AMI)) {
      Check(IsValidAlignment(AMT->getSourceAlign()),
            "incorrect alignment of the source argument", Call);
    }
    break;
  }
  case Intrinsic::call_preallocated_setup: {
    auto *NumArgs = dyn_cast<ConstantInt>(Call.getArgOperand(0));
    Check(NumArgs != nullptr,
          "llvm.call.preallocated.setup argument must be a constant");
    bool FoundCall = false;
    for (User *U : Call.users()) {
      auto *UseCall = dyn_cast<CallBase>(U);
      Check(UseCall != nullptr,
            "Uses of llvm.call.preallocated.setup must be calls");
      const Function *Fn = UseCall->getCalledFunction();
      if (Fn && Fn->getIntrinsicID() == Intrinsic::call_preallocated_arg) {
        auto *AllocArgIndex = dyn_cast<ConstantInt>(UseCall->getArgOperand(1));
        Check(AllocArgIndex != nullptr,
              "llvm.call.preallocated.alloc arg index must be a constant");
        auto AllocArgIndexInt = AllocArgIndex->getValue();
        Check(AllocArgIndexInt.sge(0) &&
                  AllocArgIndexInt.slt(NumArgs->getValue()),
              "llvm.call.preallocated.alloc arg index must be between 0 and "
              "corresponding "
              "llvm.call.preallocated.setup's argument count");
      } else if (Fn && Fn->getIntrinsicID() ==
                           Intrinsic::call_preallocated_teardown) {
        // nothing to do
      } else {
        Check(!FoundCall, "Can have at most one call corresponding to a "
                          "llvm.call.preallocated.setup");
        FoundCall = true;
        size_t NumPreallocatedArgs = 0;
        for (unsigned i = 0; i < UseCall->arg_size(); i++) {
          if (UseCall->paramHasAttr(i, Attribute::Preallocated)) {
            ++NumPreallocatedArgs;
          }
        }
        Check(NumPreallocatedArgs != 0,
              "cannot use preallocated intrinsics on a call without "
              "preallocated arguments");
        Check(NumArgs->equalsInt(NumPreallocatedArgs),
              "llvm.call.preallocated.setup arg size must be equal to number "
              "of preallocated arguments "
              "at call site",
              Call, *UseCall);
        // getOperandBundle() cannot be called if more than one of the operand
        // bundle exists. There is already a check elsewhere for this, so skip
        // here if we see more than one.
        if (UseCall->countOperandBundlesOfType(LLVMContext::OB_preallocated) >
            1) {
          return;
        }
        auto PreallocatedBundle =
            UseCall->getOperandBundle(LLVMContext::OB_preallocated);
        Check(PreallocatedBundle,
              "Use of llvm.call.preallocated.setup outside intrinsics "
              "must be in \"preallocated\" operand bundle");
        Check(PreallocatedBundle->Inputs.front().get() == &Call,
              "preallocated bundle must have token from corresponding "
              "llvm.call.preallocated.setup");
      }
    }
    break;
  }
  case Intrinsic::call_preallocated_arg: {
    auto *Token = dyn_cast<CallBase>(Call.getArgOperand(0));
    Check(Token && Token->getCalledFunction()->getIntrinsicID() ==
                       Intrinsic::call_preallocated_setup,
          "llvm.call.preallocated.arg token argument must be a "
          "llvm.call.preallocated.setup");
    Check(Call.hasFnAttr(Attribute::Preallocated),
          "llvm.call.preallocated.arg must be called with a \"preallocated\" "
          "call site attribute");
    break;
  }
  case Intrinsic::call_preallocated_teardown: {
    auto *Token = dyn_cast<CallBase>(Call.getArgOperand(0));
    Check(Token && Token->getCalledFunction()->getIntrinsicID() ==
                       Intrinsic::call_preallocated_setup,
          "llvm.call.preallocated.teardown token argument must be a "
          "llvm.call.preallocated.setup");
    break;
  }
  case Intrinsic::gcroot:
  case Intrinsic::gcwrite:
  case Intrinsic::gcread:
    if (ID == Intrinsic::gcroot) {
      AllocaInst *AI =
          dyn_cast<AllocaInst>(Call.getArgOperand(0)->stripPointerCasts());
      Check(AI, "llvm.gcroot parameter #1 must be an alloca.", Call);
      Check(isa<Constant>(Call.getArgOperand(1)),
            "llvm.gcroot parameter #2 must be a constant.", Call);
      if (!AI->getAllocatedType()->isPointerTy()) {
        Check(!isa<ConstantPointerNull>(Call.getArgOperand(1)),
              "llvm.gcroot parameter #1 must either be a pointer alloca, "
              "or argument #2 must be a non-null constant.",
              Call);
      }
    }

    Check(Call.getParent()->getParent()->hasGC(),
          "Enclosing function does not use GC.", Call);
    break;
  case Intrinsic::init_trampoline:
    Check(isa<Function>(Call.getArgOperand(1)->stripPointerCasts()),
          "llvm.init_trampoline parameter #2 must resolve to a function.",
          Call);
    break;
  case Intrinsic::prefetch:
    Check(cast<ConstantInt>(Call.getArgOperand(1))->getZExtValue() < 2,
          "rw argument to llvm.prefetch must be 0-1", Call);
    Check(cast<ConstantInt>(Call.getArgOperand(2))->getZExtValue() < 4,
          "locality argument to llvm.prefetch must be 0-3", Call);
    Check(cast<ConstantInt>(Call.getArgOperand(3))->getZExtValue() < 2,
          "cache type argument to llvm.prefetch must be 0-1", Call);
    break;
  case Intrinsic::stackprotector:
    Check(isa<AllocaInst>(Call.getArgOperand(1)->stripPointerCasts()),
          "llvm.stackprotector parameter #2 must resolve to an alloca.", Call);
    break;
  case Intrinsic::localescape: {
    BasicBlock *BB = Call.getParent();
    Check(BB->isEntryBlock(), "llvm.localescape used outside of entry block",
          Call);
    Check(!SawFrameEscape, "multiple calls to llvm.localescape in one function",
          Call);
    for (Value *Arg : Call.args()) {
      if (isa<ConstantPointerNull>(Arg))
        continue; // Null values are allowed as placeholders.
      auto *AI = dyn_cast<AllocaInst>(Arg->stripPointerCasts());
      Check(AI && AI->isStaticAlloca(),
            "llvm.localescape only accepts static allocas", Call);
    }
    FrameEscapeInfo[BB->getParent()].first = Call.arg_size();
    SawFrameEscape = true;
    break;
  }
  case Intrinsic::localrecover: {
    Value *FnArg = Call.getArgOperand(0)->stripPointerCasts();
    Function *Fn = dyn_cast<Function>(FnArg);
    Check(Fn && !Fn->isDeclaration(),
          "llvm.localrecover first "
          "argument must be function defined in this module",
          Call);
    auto *IdxArg = cast<ConstantInt>(Call.getArgOperand(2));
    auto &Entry = FrameEscapeInfo[Fn];
    Entry.second = unsigned(
        std::max(uint64_t(Entry.second), IdxArg->getLimitedValue(~0U) + 1));
    break;
  }

  case Intrinsic::experimental_gc_statepoint:
    if (auto *CI = dyn_cast<CallInst>(&Call))
      Check(!CI->isInlineAsm(),
            "gc.statepoint support for inline assembly unimplemented", CI);
    Check(Call.getParent()->getParent()->hasGC(),
          "Enclosing function does not use GC.", Call);

    verifyStatepoint(Call);
    break;
  case Intrinsic::experimental_gc_result: {
    Check(Call.getParent()->getParent()->hasGC(),
          "Enclosing function does not use GC.", Call);

    auto *Statepoint = Call.getArgOperand(0);
    if (isa<UndefValue>(Statepoint))
      break;

    // Are we tied to a statepoint properly?
    const auto *StatepointCall = dyn_cast<CallBase>(Statepoint);
    const Function *StatepointFn =
        StatepointCall ? StatepointCall->getCalledFunction() : nullptr;
    Check(StatepointFn && StatepointFn->isDeclaration() &&
              StatepointFn->getIntrinsicID() ==
                  Intrinsic::experimental_gc_statepoint,
          "gc.result operand #1 must be from a statepoint", Call,
          Call.getArgOperand(0));

    // Check that result type matches wrapped callee.
    auto *TargetFuncType =
        cast<FunctionType>(StatepointCall->getParamElementType(2));
    Check(Call.getType() == TargetFuncType->getReturnType(),
          "gc.result result type does not match wrapped callee", Call);
    break;
  }
  case Intrinsic::experimental_gc_relocate: {
    Check(Call.arg_size() == 3, "wrong number of arguments", Call);

    Check(isa<PointerType>(Call.getType()->getScalarType()),
          "gc.relocate must return a pointer or a vector of pointers", Call);

    // Check that this relocate is correctly tied to the statepoint

    // This is case for relocate on the unwinding path of an invoke statepoint
    if (LandingPadInst *LandingPad =
            dyn_cast<LandingPadInst>(Call.getArgOperand(0))) {

      const BasicBlock *InvokeBB =
          LandingPad->getParent()->getUniquePredecessor();

      // Landingpad relocates should have only one predecessor with invoke
      // statepoint terminator
      Check(InvokeBB, "safepoints should have unique landingpads",
            LandingPad->getParent());
      Check(InvokeBB->getTerminator(), "safepoint block should be well formed",
            InvokeBB);
      Check(isa<GCStatepointInst>(InvokeBB->getTerminator()),
            "gc relocate should be linked to a statepoint", InvokeBB);
    } else {
      // In all other cases relocate should be tied to the statepoint directly.
      // This covers relocates on a normal return path of invoke statepoint and
      // relocates of a call statepoint.
      auto *Token = Call.getArgOperand(0);
      Check(isa<GCStatepointInst>(Token) || isa<UndefValue>(Token),
            "gc relocate is incorrectly tied to the statepoint", Call, Token);
    }

    // Verify rest of the relocate arguments.
    const Value &StatepointCall = *cast<GCRelocateInst>(Call).getStatepoint();

    // Both the base and derived must be piped through the safepoint.
    Value *Base = Call.getArgOperand(1);
    Check(isa<ConstantInt>(Base),
          "gc.relocate operand #2 must be integer offset", Call);

    Value *Derived = Call.getArgOperand(2);
    Check(isa<ConstantInt>(Derived),
          "gc.relocate operand #3 must be integer offset", Call);

    const uint64_t BaseIndex = cast<ConstantInt>(Base)->getZExtValue();
    const uint64_t DerivedIndex = cast<ConstantInt>(Derived)->getZExtValue();

    // Check the bounds
    if (isa<UndefValue>(StatepointCall))
      break;
    if (auto Opt = cast<GCStatepointInst>(StatepointCall)
                       .getOperandBundle(LLVMContext::OB_gc_live)) {
      Check(BaseIndex < Opt->Inputs.size(),
            "gc.relocate: statepoint base index out of bounds", Call);
      Check(DerivedIndex < Opt->Inputs.size(),
            "gc.relocate: statepoint derived index out of bounds", Call);
    }

    // Relocated value must be either a pointer type or vector-of-pointer type,
    // but gc_relocate does not need to return the same pointer type as the
    // relocated pointer. It can be casted to the correct type later if it's
    // desired. However, they must have the same address space and 'vectorness'
    GCRelocateInst &Relocate = cast<GCRelocateInst>(Call);
    auto *ResultType = Call.getType();
    auto *DerivedType = Relocate.getDerivedPtr()->getType();
    auto *BaseType = Relocate.getBasePtr()->getType();

    Check(BaseType->isPtrOrPtrVectorTy(),
          "gc.relocate: relocated value must be a pointer", Call);
    Check(DerivedType->isPtrOrPtrVectorTy(),
          "gc.relocate: relocated value must be a pointer", Call);

    Check(ResultType->isVectorTy() == DerivedType->isVectorTy(),
          "gc.relocate: vector relocates to vector and pointer to pointer",
          Call);
    Check(
        ResultType->getPointerAddressSpace() ==
            DerivedType->getPointerAddressSpace(),
        "gc.relocate: relocating a pointer shouldn't change its address space",
        Call);

    auto GC = llvm::getGCStrategy(Relocate.getFunction()->getGC());
    Check(GC, "gc.relocate: calling function must have GCStrategy",
          Call.getFunction());
    if (GC) {
      auto isGCPtr = [&GC](Type *PTy) {
        return GC->isGCManagedPointer(PTy->getScalarType()).value_or(true);
      };
      Check(isGCPtr(ResultType), "gc.relocate: must return gc pointer", Call);
      Check(isGCPtr(BaseType),
            "gc.relocate: relocated value must be a gc pointer", Call);
      Check(isGCPtr(DerivedType),
            "gc.relocate: relocated value must be a gc pointer", Call);
    }
    break;
  }
  case Intrinsic::experimental_patchpoint: {
    if (Call.getCallingConv() == CallingConv::AnyReg) {
      Check(Call.getType()->isSingleValueType(),
            "patchpoint: invalid return type used with anyregcc", Call);
    }
    break;
  }
  case Intrinsic::eh_exceptioncode:
  case Intrinsic::eh_exceptionpointer: {
    Check(isa<CatchPadInst>(Call.getArgOperand(0)),
          "eh.exceptionpointer argument must be a catchpad", Call);
    break;
  }
  case Intrinsic::get_active_lane_mask: {
    Check(Call.getType()->isVectorTy(),
          "get_active_lane_mask: must return a "
          "vector",
          Call);
    auto *ElemTy = Call.getType()->getScalarType();
    Check(ElemTy->isIntegerTy(1),
          "get_active_lane_mask: element type is not "
          "i1",
          Call);
    break;
  }
  case Intrinsic::experimental_get_vector_length: {
    ConstantInt *VF = cast<ConstantInt>(Call.getArgOperand(1));
    Check(!VF->isNegative() && !VF->isZero(),
          "get_vector_length: VF must be positive", Call);
    break;
  }
  case Intrinsic::masked_load: {
    Check(Call.getType()->isVectorTy(), "masked_load: must return a vector",
          Call);

    ConstantInt *Alignment = cast<ConstantInt>(Call.getArgOperand(1));
    Value *Mask = Call.getArgOperand(2);
    Value *PassThru = Call.getArgOperand(3);
    Check(Mask->getType()->isVectorTy(), "masked_load: mask must be vector",
          Call);
    Check(Alignment->getValue().isPowerOf2(),
          "masked_load: alignment must be a power of 2", Call);
    Check(PassThru->getType() == Call.getType(),
          "masked_load: pass through and return type must match", Call);
    Check(cast<VectorType>(Mask->getType())->getElementCount() ==
              cast<VectorType>(Call.getType())->getElementCount(),
          "masked_load: vector mask must be same length as return", Call);
    break;
  }
  case Intrinsic::masked_store: {
    Value *Val = Call.getArgOperand(0);
    ConstantInt *Alignment = cast<ConstantInt>(Call.getArgOperand(2));
    Value *Mask = Call.getArgOperand(3);
    Check(Mask->getType()->isVectorTy(), "masked_store: mask must be vector",
          Call);
    Check(Alignment->getValue().isPowerOf2(),
          "masked_store: alignment must be a power of 2", Call);
    Check(cast<VectorType>(Mask->getType())->getElementCount() ==
              cast<VectorType>(Val->getType())->getElementCount(),
          "masked_store: vector mask must be same length as value", Call);
    break;
  }

  case Intrinsic::masked_gather: {
    const APInt &Alignment =
        cast<ConstantInt>(Call.getArgOperand(1))->getValue();
    Check(Alignment.isZero() || Alignment.isPowerOf2(),
          "masked_gather: alignment must be 0 or a power of 2", Call);
    break;
  }
  case Intrinsic::masked_scatter: {
    const APInt &Alignment =
        cast<ConstantInt>(Call.getArgOperand(2))->getValue();
    Check(Alignment.isZero() || Alignment.isPowerOf2(),
          "masked_scatter: alignment must be 0 or a power of 2", Call);
    break;
  }

  case Intrinsic::experimental_guard: {
    Check(isa<CallInst>(Call), "experimental_guard cannot be invoked", Call);
    Check(Call.countOperandBundlesOfType(LLVMContext::OB_deopt) == 1,
          "experimental_guard must have exactly one "
          "\"deopt\" operand bundle");
    break;
  }

  case Intrinsic::experimental_deoptimize: {
    Check(isa<CallInst>(Call), "experimental_deoptimize cannot be invoked",
          Call);
    Check(Call.countOperandBundlesOfType(LLVMContext::OB_deopt) == 1,
          "experimental_deoptimize must have exactly one "
          "\"deopt\" operand bundle");
    Check(Call.getType() == Call.getFunction()->getReturnType(),
          "experimental_deoptimize return type must match caller return type");

    if (isa<CallInst>(Call)) {
      auto *RI = dyn_cast<ReturnInst>(Call.getNextNode());
      Check(RI,
            "calls to experimental_deoptimize must be followed by a return");

      if (!Call.getType()->isVoidTy() && RI)
        Check(RI->getReturnValue() == &Call,
              "calls to experimental_deoptimize must be followed by a return "
              "of the value computed by experimental_deoptimize");
    }

    break;
  }
  case Intrinsic::vastart: {
    Check(Call.getFunction()->isVarArg(),
          "va_start called in a non-varargs function");
    break;
  }
  case Intrinsic::vector_reduce_and:
  case Intrinsic::vector_reduce_or:
  case Intrinsic::vector_reduce_xor:
  case Intrinsic::vector_reduce_add:
  case Intrinsic::vector_reduce_mul:
  case Intrinsic::vector_reduce_smax:
  case Intrinsic::vector_reduce_smin:
  case Intrinsic::vector_reduce_umax:
  case Intrinsic::vector_reduce_umin: {
    Type *ArgTy = Call.getArgOperand(0)->getType();
    Check(ArgTy->isIntOrIntVectorTy() && ArgTy->isVectorTy(),
          "Intrinsic has incorrect argument type!");
    break;
  }
  case Intrinsic::vector_reduce_fmax:
  case Intrinsic::vector_reduce_fmin: {
    Type *ArgTy = Call.getArgOperand(0)->getType();
    Check(ArgTy->isFPOrFPVectorTy() && ArgTy->isVectorTy(),
          "Intrinsic has incorrect argument type!");
    break;
  }
  case Intrinsic::vector_reduce_fadd:
  case Intrinsic::vector_reduce_fmul: {
    // Unlike the other reductions, the first argument is a start value. The
    // second argument is the vector to be reduced.
    Type *ArgTy = Call.getArgOperand(1)->getType();
    Check(ArgTy->isFPOrFPVectorTy() && ArgTy->isVectorTy(),
          "Intrinsic has incorrect argument type!");
    break;
  }
  case Intrinsic::smul_fix:
  case Intrinsic::smul_fix_sat:
  case Intrinsic::umul_fix:
  case Intrinsic::umul_fix_sat:
  case Intrinsic::sdiv_fix:
  case Intrinsic::sdiv_fix_sat:
  case Intrinsic::udiv_fix:
  case Intrinsic::udiv_fix_sat: {
    Value *Op1 = Call.getArgOperand(0);
    Value *Op2 = Call.getArgOperand(1);
    Check(Op1->getType()->isIntOrIntVectorTy(),
          "first operand of [us][mul|div]_fix[_sat] must be an int type or "
          "vector of ints");
    Check(Op2->getType()->isIntOrIntVectorTy(),
          "second operand of [us][mul|div]_fix[_sat] must be an int type or "
          "vector of ints");

    auto *Op3 = cast<ConstantInt>(Call.getArgOperand(2));
    Check(Op3->getType()->isIntegerTy(),
          "third operand of [us][mul|div]_fix[_sat] must be an int type");
    Check(Op3->getBitWidth() <= 32,
          "third operand of [us][mul|div]_fix[_sat] must fit within 32 bits");

    if (ID == Intrinsic::smul_fix || ID == Intrinsic::smul_fix_sat ||
        ID == Intrinsic::sdiv_fix || ID == Intrinsic::sdiv_fix_sat) {
      Check(Op3->getZExtValue() < Op1->getType()->getScalarSizeInBits(),
            "the scale of s[mul|div]_fix[_sat] must be less than the width of "
            "the operands");
    } else {
      Check(Op3->getZExtValue() <= Op1->getType()->getScalarSizeInBits(),
            "the scale of u[mul|div]_fix[_sat] must be less than or equal "
            "to the width of the operands");
    }
    break;
  }
  case Intrinsic::lrint:
  case Intrinsic::llrint: {
    Type *ValTy = Call.getArgOperand(0)->getType();
    Type *ResultTy = Call.getType();
    Check(
        ValTy->isFPOrFPVectorTy() && ResultTy->isIntOrIntVectorTy(),
        "llvm.lrint, llvm.llrint: argument must be floating-point or vector "
        "of floating-points, and result must be integer or vector of integers",
        &Call);
    Check(ValTy->isVectorTy() == ResultTy->isVectorTy(),
          "llvm.lrint, llvm.llrint: argument and result disagree on vector use",
          &Call);
    if (ValTy->isVectorTy()) {
      Check(cast<VectorType>(ValTy)->getElementCount() ==
                cast<VectorType>(ResultTy)->getElementCount(),
            "llvm.lrint, llvm.llrint: argument must be same length as result",
            &Call);
    }
    break;
  }
  case Intrinsic::lround:
  case Intrinsic::llround: {
    Type *ValTy = Call.getArgOperand(0)->getType();
    Type *ResultTy = Call.getType();
    Check(!ValTy->isVectorTy() && !ResultTy->isVectorTy(),
          "Intrinsic does not support vectors", &Call);
    break;
  }
  case Intrinsic::bswap: {
    Type *Ty = Call.getType();
    unsigned Size = Ty->getScalarSizeInBits();
    Check(Size % 16 == 0, "bswap must be an even number of bytes", &Call);
    break;
  }
  case Intrinsic::invariant_start: {
    ConstantInt *InvariantSize = dyn_cast<ConstantInt>(Call.getArgOperand(0));
    Check(InvariantSize &&
              (!InvariantSize->isNegative() || InvariantSize->isMinusOne()),
          "invariant_start parameter must be -1, 0 or a positive number",
          &Call);
    break;
  }
  case Intrinsic::matrix_multiply:
  case Intrinsic::matrix_transpose:
  case Intrinsic::matrix_column_major_load:
  case Intrinsic::matrix_column_major_store: {
    Function *IF = Call.getCalledFunction();
    ConstantInt *Stride = nullptr;
    ConstantInt *NumRows;
    ConstantInt *NumColumns;
    VectorType *ResultTy;
    Type *Op0ElemTy = nullptr;
    Type *Op1ElemTy = nullptr;
    switch (ID) {
    case Intrinsic::matrix_multiply: {
      NumRows = cast<ConstantInt>(Call.getArgOperand(2));
      ConstantInt *N = cast<ConstantInt>(Call.getArgOperand(3));
      NumColumns = cast<ConstantInt>(Call.getArgOperand(4));
      Check(cast<FixedVectorType>(Call.getArgOperand(0)->getType())
                    ->getNumElements() ==
                NumRows->getZExtValue() * N->getZExtValue(),
            "First argument of a matrix operation does not match specified "
            "shape!");
      Check(cast<FixedVectorType>(Call.getArgOperand(1)->getType())
                    ->getNumElements() ==
                N->getZExtValue() * NumColumns->getZExtValue(),
            "Second argument of a matrix operation does not match specified "
            "shape!");

      ResultTy = cast<VectorType>(Call.getType());
      Op0ElemTy =
          cast<VectorType>(Call.getArgOperand(0)->getType())->getElementType();
      Op1ElemTy =
          cast<VectorType>(Call.getArgOperand(1)->getType())->getElementType();
      break;
    }
    case Intrinsic::matrix_transpose:
      NumRows = cast<ConstantInt>(Call.getArgOperand(1));
      NumColumns = cast<ConstantInt>(Call.getArgOperand(2));
      ResultTy = cast<VectorType>(Call.getType());
      Op0ElemTy =
          cast<VectorType>(Call.getArgOperand(0)->getType())->getElementType();
      break;
    case Intrinsic::matrix_column_major_load: {
      Stride = dyn_cast<ConstantInt>(Call.getArgOperand(1));
      NumRows = cast<ConstantInt>(Call.getArgOperand(3));
      NumColumns = cast<ConstantInt>(Call.getArgOperand(4));
      ResultTy = cast<VectorType>(Call.getType());
      break;
    }
    case Intrinsic::matrix_column_major_store: {
      Stride = dyn_cast<ConstantInt>(Call.getArgOperand(2));
      NumRows = cast<ConstantInt>(Call.getArgOperand(4));
      NumColumns = cast<ConstantInt>(Call.getArgOperand(5));
      ResultTy = cast<VectorType>(Call.getArgOperand(0)->getType());
      Op0ElemTy =
          cast<VectorType>(Call.getArgOperand(0)->getType())->getElementType();
      break;
    }
    default:
      llvm_unreachable("unexpected intrinsic");
    }

    Check(ResultTy->getElementType()->isIntegerTy() ||
              ResultTy->getElementType()->isFloatingPointTy(),
          "Result type must be an integer or floating-point type!", IF);

    if (Op0ElemTy)
      Check(ResultTy->getElementType() == Op0ElemTy,
            "Vector element type mismatch of the result and first operand "
            "vector!",
            IF);

    if (Op1ElemTy)
      Check(ResultTy->getElementType() == Op1ElemTy,
            "Vector element type mismatch of the result and second operand "
            "vector!",
            IF);

    Check(cast<FixedVectorType>(ResultTy)->getNumElements() ==
              NumRows->getZExtValue() * NumColumns->getZExtValue(),
          "Result of a matrix operation does not fit in the returned vector!");

    if (Stride)
      Check(Stride->getZExtValue() >= NumRows->getZExtValue(),
            "Stride must be greater or equal than the number of rows!", IF);

    break;
  }
  case Intrinsic::vector_splice: {
    VectorType *VecTy = cast<VectorType>(Call.getType());
    int64_t Idx = cast<ConstantInt>(Call.getArgOperand(2))->getSExtValue();
    int64_t KnownMinNumElements = VecTy->getElementCount().getKnownMinValue();
    if (Call.getParent() && Call.getParent()->getParent()) {
      AttributeList Attrs = Call.getParent()->getParent()->getAttributes();
      if (Attrs.hasFnAttr(Attribute::VScaleRange))
        KnownMinNumElements *= Attrs.getFnAttrs().getVScaleRangeMin();
    }
    Check((Idx < 0 && std::abs(Idx) <= KnownMinNumElements) ||
              (Idx >= 0 && Idx < KnownMinNumElements),
          "The splice index exceeds the range [-VL, VL-1] where VL is the "
          "known minimum number of elements in the vector. For scalable "
          "vectors the minimum number of elements is determined from "
          "vscale_range.",
          &Call);
    break;
  }
  case Intrinsic::experimental_stepvector: {
    VectorType *VecTy = dyn_cast<VectorType>(Call.getType());
    Check(VecTy && VecTy->getScalarType()->isIntegerTy() &&
              VecTy->getScalarSizeInBits() >= 8,
          "experimental_stepvector only supported for vectors of integers "
          "with a bitwidth of at least 8.",
          &Call);
    break;
  }
  case Intrinsic::vector_insert: {
    Value *Vec = Call.getArgOperand(0);
    Value *SubVec = Call.getArgOperand(1);
    Value *Idx = Call.getArgOperand(2);
    unsigned IdxN = cast<ConstantInt>(Idx)->getZExtValue();

    VectorType *VecTy = cast<VectorType>(Vec->getType());
    VectorType *SubVecTy = cast<VectorType>(SubVec->getType());

    ElementCount VecEC = VecTy->getElementCount();
    ElementCount SubVecEC = SubVecTy->getElementCount();
    Check(VecTy->getElementType() == SubVecTy->getElementType(),
          "vector_insert parameters must have the same element "
          "type.",
          &Call);
    Check(IdxN % SubVecEC.getKnownMinValue() == 0,
          "vector_insert index must be a constant multiple of "
          "the subvector's known minimum vector length.");

    // If this insertion is not the 'mixed' case where a fixed vector is
    // inserted into a scalable vector, ensure that the insertion of the
    // subvector does not overrun the parent vector.
    if (VecEC.isScalable() == SubVecEC.isScalable()) {
      Check(IdxN < VecEC.getKnownMinValue() &&
                IdxN + SubVecEC.getKnownMinValue() <= VecEC.getKnownMinValue(),
            "subvector operand of vector_insert would overrun the "
            "vector being inserted into.");
    }
    break;
  }
  case Intrinsic::vector_extract: {
    Value *Vec = Call.getArgOperand(0);
    Value *Idx = Call.getArgOperand(1);
    unsigned IdxN = cast<ConstantInt>(Idx)->getZExtValue();

    VectorType *ResultTy = cast<VectorType>(Call.getType());
    VectorType *VecTy = cast<VectorType>(Vec->getType());

    ElementCount VecEC = VecTy->getElementCount();
    ElementCount ResultEC = ResultTy->getElementCount();

    Check(ResultTy->getElementType() == VecTy->getElementType(),
          "vector_extract result must have the same element "
          "type as the input vector.",
          &Call);
    Check(IdxN % ResultEC.getKnownMinValue() == 0,
          "vector_extract index must be a constant multiple of "
          "the result type's known minimum vector length.");

    // If this extraction is not the 'mixed' case where a fixed vector is
    // extracted from a scalable vector, ensure that the extraction does not
    // overrun the parent vector.
    if (VecEC.isScalable() == ResultEC.isScalable()) {
      Check(IdxN < VecEC.getKnownMinValue() &&
                IdxN + ResultEC.getKnownMinValue() <= VecEC.getKnownMinValue(),
            "vector_extract would overrun.");
    }
    break;
  }
  case Intrinsic::experimental_vector_partial_reduce_add: {
    VectorType *AccTy = cast<VectorType>(Call.getArgOperand(0)->getType());
    VectorType *VecTy = cast<VectorType>(Call.getArgOperand(1)->getType());

    unsigned VecWidth = VecTy->getElementCount().getKnownMinValue();
    unsigned AccWidth = AccTy->getElementCount().getKnownMinValue();

    Check((VecWidth % AccWidth) == 0,
          "Invalid vector widths for partial "
          "reduction. The width of the input vector "
          "must be a positive integer multiple of "
          "the width of the accumulator vector.");
    break;
  }
  case Intrinsic::experimental_noalias_scope_decl: {
    NoAliasScopeDecls.push_back(cast<IntrinsicInst>(&Call));
    break;
  }
  case Intrinsic::preserve_array_access_index:
  case Intrinsic::preserve_struct_access_index:
  case Intrinsic::aarch64_ldaxr:
  case Intrinsic::aarch64_ldxr:
  case Intrinsic::arm_ldaex:
  case Intrinsic::arm_ldrex: {
    Type *ElemTy = Call.getParamElementType(0);
    Check(ElemTy, "Intrinsic requires elementtype attribute on first argument.",
          &Call);
    break;
  }
  case Intrinsic::aarch64_stlxr:
  case Intrinsic::aarch64_stxr:
  case Intrinsic::arm_stlex:
  case Intrinsic::arm_strex: {
    Type *ElemTy = Call.getAttributes().getParamElementType(1);
    Check(ElemTy,
          "Intrinsic requires elementtype attribute on second argument.",
          &Call);
    break;
  }
  case Intrinsic::aarch64_prefetch: {
    Check(cast<ConstantInt>(Call.getArgOperand(1))->getZExtValue() < 2,
          "write argument to llvm.aarch64.prefetch must be 0 or 1", Call);
    Check(cast<ConstantInt>(Call.getArgOperand(2))->getZExtValue() < 4,
          "target argument to llvm.aarch64.prefetch must be 0-3", Call);
    Check(cast<ConstantInt>(Call.getArgOperand(3))->getZExtValue() < 2,
          "stream argument to llvm.aarch64.prefetch must be 0 or 1", Call);
    Check(cast<ConstantInt>(Call.getArgOperand(4))->getZExtValue() < 2,
          "isdata argument to llvm.aarch64.prefetch must be 0 or 1", Call);
    break;
  }
  case Intrinsic::callbr_landingpad: {
    const auto *CBR = dyn_cast<CallBrInst>(Call.getOperand(0));
    Check(CBR, "intrinstic requires callbr operand", &Call);
    if (!CBR)
      break;

    const BasicBlock *LandingPadBB = Call.getParent();
    const BasicBlock *PredBB = LandingPadBB->getUniquePredecessor();
    if (!PredBB) {
      CheckFailed("Intrinsic in block must have 1 unique predecessor", &Call);
      break;
    }
    if (!isa<CallBrInst>(PredBB->getTerminator())) {
      CheckFailed("Intrinsic must have corresponding callbr in predecessor",
                  &Call);
      break;
    }
    Check(llvm::any_of(CBR->getIndirectDests(),
                       [LandingPadBB](const BasicBlock *IndDest) {
                         return IndDest == LandingPadBB;
                       }),
          "Intrinsic's corresponding callbr must have intrinsic's parent basic "
          "block in indirect destination list",
          &Call);
    const Instruction &First = *LandingPadBB->begin();
    Check(&First == &Call, "No other instructions may proceed intrinsic",
          &Call);
    break;
  }
  case Intrinsic::amdgcn_cs_chain: {
    auto CallerCC = Call.getCaller()->getCallingConv();
    switch (CallerCC) {
    case CallingConv::AMDGPU_CS:
    case CallingConv::AMDGPU_CS_Chain:
    case CallingConv::AMDGPU_CS_ChainPreserve:
      break;
    default:
      CheckFailed("Intrinsic can only be used from functions with the "
                  "amdgpu_cs, amdgpu_cs_chain or amdgpu_cs_chain_preserve "
                  "calling conventions",
                  &Call);
      break;
    }

    Check(Call.paramHasAttr(2, Attribute::InReg),
          "SGPR arguments must have the `inreg` attribute", &Call);
    Check(!Call.paramHasAttr(3, Attribute::InReg),
          "VGPR arguments must not have the `inreg` attribute", &Call);
    break;
  }
  case Intrinsic::amdgcn_set_inactive_chain_arg: {
    auto CallerCC = Call.getCaller()->getCallingConv();
    switch (CallerCC) {
    case CallingConv::AMDGPU_CS_Chain:
    case CallingConv::AMDGPU_CS_ChainPreserve:
      break;
    default:
      CheckFailed("Intrinsic can only be used from functions with the "
                  "amdgpu_cs_chain or amdgpu_cs_chain_preserve "
                  "calling conventions",
                  &Call);
      break;
    }

    unsigned InactiveIdx = 1;
    Check(!Call.paramHasAttr(InactiveIdx, Attribute::InReg),
          "Value for inactive lanes must not have the `inreg` attribute",
          &Call);
    Check(isa<Argument>(Call.getArgOperand(InactiveIdx)),
          "Value for inactive lanes must be a function argument", &Call);
    Check(!cast<Argument>(Call.getArgOperand(InactiveIdx))->hasInRegAttr(),
          "Value for inactive lanes must be a VGPR function argument", &Call);
    break;
  }
  case Intrinsic::nvvm_setmaxnreg_inc_sync_aligned_u32:
  case Intrinsic::nvvm_setmaxnreg_dec_sync_aligned_u32: {
    Value *V = Call.getArgOperand(0);
    unsigned RegCount = cast<ConstantInt>(V)->getZExtValue();
    Check(RegCount % 8 == 0,
          "reg_count argument to nvvm.setmaxnreg must be in multiples of 8");
    Check((RegCount >= 24 && RegCount <= 256),
          "reg_count argument to nvvm.setmaxnreg must be within [24, 256]");
    break;
  }
  case Intrinsic::experimental_convergence_entry:
  case Intrinsic::experimental_convergence_anchor:
    break;
  case Intrinsic::experimental_convergence_loop:
    break;
  case Intrinsic::ptrmask: {
    Type *Ty0 = Call.getArgOperand(0)->getType();
    Type *Ty1 = Call.getArgOperand(1)->getType();
    Check(Ty0->isPtrOrPtrVectorTy(),
          "llvm.ptrmask intrinsic first argument must be pointer or vector "
          "of pointers",
          &Call);
    Check(
        Ty0->isVectorTy() == Ty1->isVectorTy(),
        "llvm.ptrmask intrinsic arguments must be both scalars or both vectors",
        &Call);
    if (Ty0->isVectorTy())
      Check(cast<VectorType>(Ty0)->getElementCount() ==
                cast<VectorType>(Ty1)->getElementCount(),
            "llvm.ptrmask intrinsic arguments must have the same number of "
            "elements",
            &Call);
    Check(DL.getIndexTypeSizeInBits(Ty0) == Ty1->getScalarSizeInBits(),
          "llvm.ptrmask intrinsic second argument bitwidth must match "
          "pointer index type size of first argument",
          &Call);
    break;
  }
  case Intrinsic::threadlocal_address: {
    const Value &Arg0 = *Call.getArgOperand(0);
    Check(isa<GlobalValue>(Arg0),
          "llvm.threadlocal.address first argument must be a GlobalValue");
    Check(cast<GlobalValue>(Arg0).isThreadLocal(),
          "llvm.threadlocal.address operand isThreadLocal() must be true");
    break;
  }
  };

  // Verify that there aren't any unmediated control transfers between funclets.
  if (IntrinsicInst::mayLowerToFunctionCall(ID)) {
    Function *F = Call.getParent()->getParent();
    if (F->hasPersonalityFn() &&
        isScopedEHPersonality(classifyEHPersonality(F->getPersonalityFn()))) {
      // Run EH funclet coloring on-demand and cache results for other intrinsic
      // calls in this function
      if (BlockEHFuncletColors.empty())
        BlockEHFuncletColors = colorEHFunclets(*F);

      // Check for catch-/cleanup-pad in first funclet block
      bool InEHFunclet = false;
      BasicBlock *CallBB = Call.getParent();
      const ColorVector &CV = BlockEHFuncletColors.find(CallBB)->second;
      assert(CV.size() > 0 && "Uncolored block");
      for (BasicBlock *ColorFirstBB : CV)
        if (dyn_cast_or_null<FuncletPadInst>(ColorFirstBB->getFirstNonPHI()))
          InEHFunclet = true;

      // Check for funclet operand bundle
      bool HasToken = false;
      for (unsigned I = 0, E = Call.getNumOperandBundles(); I != E; ++I)
        if (Call.getOperandBundleAt(I).getTagID() == LLVMContext::OB_funclet)
          HasToken = true;

      // This would cause silent code truncation in WinEHPrepare
      if (InEHFunclet)
        Check(HasToken, "Missing funclet token on intrinsic call", &Call);
    }
  }
}

/// Carefully grab the subprogram from a local scope.
///
/// This carefully grabs the subprogram from a local scope, avoiding the
/// built-in assertions that would typically fire.
static DISubprogram *getSubprogram(Metadata *LocalScope) {
  if (!LocalScope)
    return nullptr;

  if (auto *SP = dyn_cast<DISubprogram>(LocalScope))
    return SP;

  if (auto *LB = dyn_cast<DILexicalBlockBase>(LocalScope))
    return getSubprogram(LB->getRawScope());

  // Just return null; broken scope chains are checked elsewhere.
  assert(!isa<DILocalScope>(LocalScope) && "Unknown type of local scope");
  return nullptr;
}

void Verifier::visit(DbgLabelRecord &DLR) {
  CheckDI(isa<DILabel>(DLR.getRawLabel()),
          "invalid #dbg_label intrinsic variable", &DLR, DLR.getRawLabel());

  // Ignore broken !dbg attachments; they're checked elsewhere.
  if (MDNode *N = DLR.getDebugLoc().getAsMDNode())
    if (!isa<DILocation>(N))
      return;

  BasicBlock *BB = DLR.getParent();
  Function *F = BB ? BB->getParent() : nullptr;

  // The scopes for variables and !dbg attachments must agree.
  DILabel *Label = DLR.getLabel();
  DILocation *Loc = DLR.getDebugLoc();
  CheckDI(Loc, "#dbg_label record requires a !dbg attachment", &DLR, BB, F);

  DISubprogram *LabelSP = getSubprogram(Label->getRawScope());
  DISubprogram *LocSP = getSubprogram(Loc->getRawScope());
  if (!LabelSP || !LocSP)
    return;

  CheckDI(LabelSP == LocSP,
          "mismatched subprogram between #dbg_label label and !dbg attachment",
          &DLR, BB, F, Label, Label->getScope()->getSubprogram(), Loc,
          Loc->getScope()->getSubprogram());
}

void Verifier::visit(DbgVariableRecord &DVR) {
  BasicBlock *BB = DVR.getParent();
  Function *F = BB->getParent();

  CheckDI(DVR.getType() == DbgVariableRecord::LocationType::Value ||
              DVR.getType() == DbgVariableRecord::LocationType::Declare ||
              DVR.getType() == DbgVariableRecord::LocationType::Assign,
          "invalid #dbg record type", &DVR, DVR.getType());

  // The location for a DbgVariableRecord must be either a ValueAsMetadata,
  // DIArgList, or an empty MDNode (which is a legacy representation for an
  // "undef" location).
  auto *MD = DVR.getRawLocation();
  CheckDI(MD && (isa<ValueAsMetadata>(MD) || isa<DIArgList>(MD) ||
                 (isa<MDNode>(MD) && !cast<MDNode>(MD)->getNumOperands())),
          "invalid #dbg record address/value", &DVR, MD);
  if (auto *VAM = dyn_cast<ValueAsMetadata>(MD))
    visitValueAsMetadata(*VAM, F);
  else if (auto *AL = dyn_cast<DIArgList>(MD))
    visitDIArgList(*AL, F);

  CheckDI(isa_and_nonnull<DILocalVariable>(DVR.getRawVariable()),
          "invalid #dbg record variable", &DVR, DVR.getRawVariable());
  visitMDNode(*DVR.getRawVariable(), AreDebugLocsAllowed::No);

  CheckDI(isa_and_nonnull<DIExpression>(DVR.getRawExpression()),
          "invalid #dbg record expression", &DVR, DVR.getRawExpression());
  visitMDNode(*DVR.getExpression(), AreDebugLocsAllowed::No);

  if (DVR.isDbgAssign()) {
    CheckDI(isa_and_nonnull<DIAssignID>(DVR.getRawAssignID()),
            "invalid #dbg_assign DIAssignID", &DVR, DVR.getRawAssignID());
    visitMDNode(*cast<DIAssignID>(DVR.getRawAssignID()),
                AreDebugLocsAllowed::No);

    const auto *RawAddr = DVR.getRawAddress();
    // Similarly to the location above, the address for an assign
    // DbgVariableRecord must be a ValueAsMetadata or an empty MDNode, which
    // represents an undef address.
    CheckDI(
        isa<ValueAsMetadata>(RawAddr) ||
            (isa<MDNode>(RawAddr) && !cast<MDNode>(RawAddr)->getNumOperands()),
        "invalid #dbg_assign address", &DVR, DVR.getRawAddress());
    if (auto *VAM = dyn_cast<ValueAsMetadata>(RawAddr))
      visitValueAsMetadata(*VAM, F);

    CheckDI(isa_and_nonnull<DIExpression>(DVR.getRawAddressExpression()),
            "invalid #dbg_assign address expression", &DVR,
            DVR.getRawAddressExpression());
    visitMDNode(*DVR.getAddressExpression(), AreDebugLocsAllowed::No);

    // All of the linked instructions should be in the same function as DVR.
    for (Instruction *I : at::getAssignmentInsts(&DVR))
      CheckDI(DVR.getFunction() == I->getFunction(),
              "inst not in same function as #dbg_assign", I, &DVR);
  }

  // This check is redundant with one in visitLocalVariable().
  DILocalVariable *Var = DVR.getVariable();
  CheckDI(isType(Var->getRawType()), "invalid type ref", Var,
          Var->getRawType());

  auto *DLNode = DVR.getDebugLoc().getAsMDNode();
  CheckDI(isa_and_nonnull<DILocation>(DLNode), "invalid #dbg record DILocation",
          &DVR, DLNode);
  DILocation *Loc = DVR.getDebugLoc();

  // The scopes for variables and !dbg attachments must agree.
  DISubprogram *VarSP = getSubprogram(Var->getRawScope());
  DISubprogram *LocSP = getSubprogram(Loc->getRawScope());
  if (!VarSP || !LocSP)
    return; // Broken scope chains are checked elsewhere.

  CheckDI(VarSP == LocSP,
          "mismatched subprogram between #dbg record variable and DILocation",
          &DVR, BB, F, Var, Var->getScope()->getSubprogram(), Loc,
          Loc->getScope()->getSubprogram());

  verifyFnArgs(DVR);
}

void Verifier::visitVPIntrinsic(VPIntrinsic &VPI) {
  if (auto *VPCast = dyn_cast<VPCastIntrinsic>(&VPI)) {
    auto *RetTy = cast<VectorType>(VPCast->getType());
    auto *ValTy = cast<VectorType>(VPCast->getOperand(0)->getType());
    Check(RetTy->getElementCount() == ValTy->getElementCount(),
          "VP cast intrinsic first argument and result vector lengths must be "
          "equal",
          *VPCast);

    switch (VPCast->getIntrinsicID()) {
    default:
      llvm_unreachable("Unknown VP cast intrinsic");
    case Intrinsic::vp_trunc:
      Check(RetTy->isIntOrIntVectorTy() && ValTy->isIntOrIntVectorTy(),
            "llvm.vp.trunc intrinsic first argument and result element type "
            "must be integer",
            *VPCast);
      Check(RetTy->getScalarSizeInBits() < ValTy->getScalarSizeInBits(),
            "llvm.vp.trunc intrinsic the bit size of first argument must be "
            "larger than the bit size of the return type",
            *VPCast);
      break;
    case Intrinsic::vp_zext:
    case Intrinsic::vp_sext:
      Check(RetTy->isIntOrIntVectorTy() && ValTy->isIntOrIntVectorTy(),
            "llvm.vp.zext or llvm.vp.sext intrinsic first argument and result "
            "element type must be integer",
            *VPCast);
      Check(RetTy->getScalarSizeInBits() > ValTy->getScalarSizeInBits(),
            "llvm.vp.zext or llvm.vp.sext intrinsic the bit size of first "
            "argument must be smaller than the bit size of the return type",
            *VPCast);
      break;
    case Intrinsic::vp_fptoui:
    case Intrinsic::vp_fptosi:
    case Intrinsic::vp_lrint:
    case Intrinsic::vp_llrint:
      Check(
          RetTy->isIntOrIntVectorTy() && ValTy->isFPOrFPVectorTy(),
          "llvm.vp.fptoui, llvm.vp.fptosi, llvm.vp.lrint or llvm.vp.llrint" "intrinsic first argument element "
          "type must be floating-point and result element type must be integer",
          *VPCast);
      break;
    case Intrinsic::vp_uitofp:
    case Intrinsic::vp_sitofp:
      Check(
          RetTy->isFPOrFPVectorTy() && ValTy->isIntOrIntVectorTy(),
          "llvm.vp.uitofp or llvm.vp.sitofp intrinsic first argument element "
          "type must be integer and result element type must be floating-point",
          *VPCast);
      break;
    case Intrinsic::vp_fptrunc:
      Check(RetTy->isFPOrFPVectorTy() && ValTy->isFPOrFPVectorTy(),
            "llvm.vp.fptrunc intrinsic first argument and result element type "
            "must be floating-point",
            *VPCast);
      Check(RetTy->getScalarSizeInBits() < ValTy->getScalarSizeInBits(),
            "llvm.vp.fptrunc intrinsic the bit size of first argument must be "
            "larger than the bit size of the return type",
            *VPCast);
      break;
    case Intrinsic::vp_fpext:
      Check(RetTy->isFPOrFPVectorTy() && ValTy->isFPOrFPVectorTy(),
            "llvm.vp.fpext intrinsic first argument and result element type "
            "must be floating-point",
            *VPCast);
      Check(RetTy->getScalarSizeInBits() > ValTy->getScalarSizeInBits(),
            "llvm.vp.fpext intrinsic the bit size of first argument must be "
            "smaller than the bit size of the return type",
            *VPCast);
      break;
    case Intrinsic::vp_ptrtoint:
      Check(RetTy->isIntOrIntVectorTy() && ValTy->isPtrOrPtrVectorTy(),
            "llvm.vp.ptrtoint intrinsic first argument element type must be "
            "pointer and result element type must be integer",
            *VPCast);
      break;
    case Intrinsic::vp_inttoptr:
      Check(RetTy->isPtrOrPtrVectorTy() && ValTy->isIntOrIntVectorTy(),
            "llvm.vp.inttoptr intrinsic first argument element type must be "
            "integer and result element type must be pointer",
            *VPCast);
      break;
    }
  }
  if (VPI.getIntrinsicID() == Intrinsic::vp_fcmp) {
    auto Pred = cast<VPCmpIntrinsic>(&VPI)->getPredicate();
    Check(CmpInst::isFPPredicate(Pred),
          "invalid predicate for VP FP comparison intrinsic", &VPI);
  }
  if (VPI.getIntrinsicID() == Intrinsic::vp_icmp) {
    auto Pred = cast<VPCmpIntrinsic>(&VPI)->getPredicate();
    Check(CmpInst::isIntPredicate(Pred),
          "invalid predicate for VP integer comparison intrinsic", &VPI);
  }
  if (VPI.getIntrinsicID() == Intrinsic::vp_is_fpclass) {
    auto TestMask = cast<ConstantInt>(VPI.getOperand(1));
    Check((TestMask->getZExtValue() & ~static_cast<unsigned>(fcAllFlags)) == 0,
          "unsupported bits for llvm.vp.is.fpclass test mask");
  }
}

void Verifier::visitConstrainedFPIntrinsic(ConstrainedFPIntrinsic &FPI) {
  unsigned NumOperands = FPI.getNonMetadataArgCount();
  bool HasRoundingMD =
      Intrinsic::hasConstrainedFPRoundingModeOperand(FPI.getIntrinsicID());

  // Add the expected number of metadata operands.
  NumOperands += (1 + HasRoundingMD);

  // Compare intrinsics carry an extra predicate metadata operand.
  if (isa<ConstrainedFPCmpIntrinsic>(FPI))
    NumOperands += 1;
  Check((FPI.arg_size() == NumOperands),
        "invalid arguments for constrained FP intrinsic", &FPI);

  switch (FPI.getIntrinsicID()) {
  case Intrinsic::experimental_constrained_lrint:
  case Intrinsic::experimental_constrained_llrint: {
    Type *ValTy = FPI.getArgOperand(0)->getType();
    Type *ResultTy = FPI.getType();
    Check(!ValTy->isVectorTy() && !ResultTy->isVectorTy(),
          "Intrinsic does not support vectors", &FPI);
    break;
  }

  case Intrinsic::experimental_constrained_lround:
  case Intrinsic::experimental_constrained_llround: {
    Type *ValTy = FPI.getArgOperand(0)->getType();
    Type *ResultTy = FPI.getType();
    Check(!ValTy->isVectorTy() && !ResultTy->isVectorTy(),
          "Intrinsic does not support vectors", &FPI);
    break;
  }

  case Intrinsic::experimental_constrained_fcmp:
  case Intrinsic::experimental_constrained_fcmps: {
    auto Pred = cast<ConstrainedFPCmpIntrinsic>(&FPI)->getPredicate();
    Check(CmpInst::isFPPredicate(Pred),
          "invalid predicate for constrained FP comparison intrinsic", &FPI);
    break;
  }

  case Intrinsic::experimental_constrained_fptosi:
  case Intrinsic::experimental_constrained_fptoui: {
    Value *Operand = FPI.getArgOperand(0);
    ElementCount SrcEC;
    Check(Operand->getType()->isFPOrFPVectorTy(),
          "Intrinsic first argument must be floating point", &FPI);
    if (auto *OperandT = dyn_cast<VectorType>(Operand->getType())) {
      SrcEC = cast<VectorType>(OperandT)->getElementCount();
    }

    Operand = &FPI;
    Check(SrcEC.isNonZero() == Operand->getType()->isVectorTy(),
          "Intrinsic first argument and result disagree on vector use", &FPI);
    Check(Operand->getType()->isIntOrIntVectorTy(),
          "Intrinsic result must be an integer", &FPI);
    if (auto *OperandT = dyn_cast<VectorType>(Operand->getType())) {
      Check(SrcEC == cast<VectorType>(OperandT)->getElementCount(),
            "Intrinsic first argument and result vector lengths must be equal",
            &FPI);
    }
    break;
  }

  case Intrinsic::experimental_constrained_sitofp:
  case Intrinsic::experimental_constrained_uitofp: {
    Value *Operand = FPI.getArgOperand(0);
    ElementCount SrcEC;
    Check(Operand->getType()->isIntOrIntVectorTy(),
          "Intrinsic first argument must be integer", &FPI);
    if (auto *OperandT = dyn_cast<VectorType>(Operand->getType())) {
      SrcEC = cast<VectorType>(OperandT)->getElementCount();
    }

    Operand = &FPI;
    Check(SrcEC.isNonZero() == Operand->getType()->isVectorTy(),
          "Intrinsic first argument and result disagree on vector use", &FPI);
    Check(Operand->getType()->isFPOrFPVectorTy(),
          "Intrinsic result must be a floating point", &FPI);
    if (auto *OperandT = dyn_cast<VectorType>(Operand->getType())) {
      Check(SrcEC == cast<VectorType>(OperandT)->getElementCount(),
            "Intrinsic first argument and result vector lengths must be equal",
            &FPI);
    }
    break;
  }

  case Intrinsic::experimental_constrained_fptrunc:
  case Intrinsic::experimental_constrained_fpext: {
    Value *Operand = FPI.getArgOperand(0);
    Type *OperandTy = Operand->getType();
    Value *Result = &FPI;
    Type *ResultTy = Result->getType();
    Check(OperandTy->isFPOrFPVectorTy(),
          "Intrinsic first argument must be FP or FP vector", &FPI);
    Check(ResultTy->isFPOrFPVectorTy(),
          "Intrinsic result must be FP or FP vector", &FPI);
    Check(OperandTy->isVectorTy() == ResultTy->isVectorTy(),
          "Intrinsic first argument and result disagree on vector use", &FPI);
    if (OperandTy->isVectorTy()) {
      Check(cast<VectorType>(OperandTy)->getElementCount() ==
                cast<VectorType>(ResultTy)->getElementCount(),
            "Intrinsic first argument and result vector lengths must be equal",
            &FPI);
    }
    if (FPI.getIntrinsicID() == Intrinsic::experimental_constrained_fptrunc) {
      Check(OperandTy->getScalarSizeInBits() > ResultTy->getScalarSizeInBits(),
            "Intrinsic first argument's type must be larger than result type",
            &FPI);
    } else {
      Check(OperandTy->getScalarSizeInBits() < ResultTy->getScalarSizeInBits(),
            "Intrinsic first argument's type must be smaller than result type",
            &FPI);
    }
    break;
  }

  default:
    break;
  }

  // If a non-metadata argument is passed in a metadata slot then the
  // error will be caught earlier when the incorrect argument doesn't
  // match the specification in the intrinsic call table. Thus, no
  // argument type check is needed here.

  Check(FPI.getExceptionBehavior().has_value(),
        "invalid exception behavior argument", &FPI);
  if (HasRoundingMD) {
    Check(FPI.getRoundingMode().has_value(), "invalid rounding mode argument",
          &FPI);
  }
}

void Verifier::visitDbgIntrinsic(StringRef Kind, DbgVariableIntrinsic &DII) {
  auto *MD = DII.getRawLocation();
  CheckDI(isa<ValueAsMetadata>(MD) || isa<DIArgList>(MD) ||
              (isa<MDNode>(MD) && !cast<MDNode>(MD)->getNumOperands()),
          "invalid llvm.dbg." + Kind + " intrinsic address/value", &DII, MD);
  CheckDI(isa<DILocalVariable>(DII.getRawVariable()),
          "invalid llvm.dbg." + Kind + " intrinsic variable", &DII,
          DII.getRawVariable());
  CheckDI(isa<DIExpression>(DII.getRawExpression()),
          "invalid llvm.dbg." + Kind + " intrinsic expression", &DII,
          DII.getRawExpression());

  if (auto *DAI = dyn_cast<DbgAssignIntrinsic>(&DII)) {
    CheckDI(isa<DIAssignID>(DAI->getRawAssignID()),
            "invalid llvm.dbg.assign intrinsic DIAssignID", &DII,
            DAI->getRawAssignID());
    const auto *RawAddr = DAI->getRawAddress();
    CheckDI(
        isa<ValueAsMetadata>(RawAddr) ||
            (isa<MDNode>(RawAddr) && !cast<MDNode>(RawAddr)->getNumOperands()),
        "invalid llvm.dbg.assign intrinsic address", &DII,
        DAI->getRawAddress());
    CheckDI(isa<DIExpression>(DAI->getRawAddressExpression()),
            "invalid llvm.dbg.assign intrinsic address expression", &DII,
            DAI->getRawAddressExpression());
    // All of the linked instructions should be in the same function as DII.
    for (Instruction *I : at::getAssignmentInsts(DAI))
      CheckDI(DAI->getFunction() == I->getFunction(),
              "inst not in same function as dbg.assign", I, DAI);
  }

  // Ignore broken !dbg attachments; they're checked elsewhere.
  if (MDNode *N = DII.getDebugLoc().getAsMDNode())
    if (!isa<DILocation>(N))
      return;

  BasicBlock *BB = DII.getParent();
  Function *F = BB ? BB->getParent() : nullptr;

  // The scopes for variables and !dbg attachments must agree.
  DILocalVariable *Var = DII.getVariable();
  DILocation *Loc = DII.getDebugLoc();
  CheckDI(Loc, "llvm.dbg." + Kind + " intrinsic requires a !dbg attachment",
          &DII, BB, F);

  DISubprogram *VarSP = getSubprogram(Var->getRawScope());
  DISubprogram *LocSP = getSubprogram(Loc->getRawScope());
  if (!VarSP || !LocSP)
    return; // Broken scope chains are checked elsewhere.

  CheckDI(VarSP == LocSP,
          "mismatched subprogram between llvm.dbg." + Kind +
              " variable and !dbg attachment",
          &DII, BB, F, Var, Var->getScope()->getSubprogram(), Loc,
          Loc->getScope()->getSubprogram());

  // This check is redundant with one in visitLocalVariable().
  CheckDI(isType(Var->getRawType()), "invalid type ref", Var,
          Var->getRawType());
  verifyFnArgs(DII);
}

void Verifier::visitDbgLabelIntrinsic(StringRef Kind, DbgLabelInst &DLI) {
  CheckDI(isa<DILabel>(DLI.getRawLabel()),
          "invalid llvm.dbg." + Kind + " intrinsic variable", &DLI,
          DLI.getRawLabel());

  // Ignore broken !dbg attachments; they're checked elsewhere.
  if (MDNode *N = DLI.getDebugLoc().getAsMDNode())
    if (!isa<DILocation>(N))
      return;

  BasicBlock *BB = DLI.getParent();
  Function *F = BB ? BB->getParent() : nullptr;

  // The scopes for variables and !dbg attachments must agree.
  DILabel *Label = DLI.getLabel();
  DILocation *Loc = DLI.getDebugLoc();
  Check(Loc, "llvm.dbg." + Kind + " intrinsic requires a !dbg attachment", &DLI,
        BB, F);

  DISubprogram *LabelSP = getSubprogram(Label->getRawScope());
  DISubprogram *LocSP = getSubprogram(Loc->getRawScope());
  if (!LabelSP || !LocSP)
    return;

  CheckDI(LabelSP == LocSP,
          "mismatched subprogram between llvm.dbg." + Kind +
              " label and !dbg attachment",
          &DLI, BB, F, Label, Label->getScope()->getSubprogram(), Loc,
          Loc->getScope()->getSubprogram());
}

void Verifier::verifyFragmentExpression(const DbgVariableIntrinsic &I) {
  DILocalVariable *V = dyn_cast_or_null<DILocalVariable>(I.getRawVariable());
  DIExpression *E = dyn_cast_or_null<DIExpression>(I.getRawExpression());

  // We don't know whether this intrinsic verified correctly.
  if (!V || !E || !E->isValid())
    return;

  // Nothing to do if this isn't a DW_OP_LLVM_fragment expression.
  auto Fragment = E->getFragmentInfo();
  if (!Fragment)
    return;

  // The frontend helps out GDB by emitting the members of local anonymous
  // unions as artificial local variables with shared storage. When SROA splits
  // the storage for artificial local variables that are smaller than the entire
  // union, the overhang piece will be outside of the allotted space for the
  // variable and this check fails.
  // FIXME: Remove this check as soon as clang stops doing this; it hides bugs.
  if (V->isArtificial())
    return;

  verifyFragmentExpression(*V, *Fragment, &I);
}
void Verifier::verifyFragmentExpression(const DbgVariableRecord &DVR) {
  DILocalVariable *V = dyn_cast_or_null<DILocalVariable>(DVR.getRawVariable());
  DIExpression *E = dyn_cast_or_null<DIExpression>(DVR.getRawExpression());

  // We don't know whether this intrinsic verified correctly.
  if (!V || !E || !E->isValid())
    return;

  // Nothing to do if this isn't a DW_OP_LLVM_fragment expression.
  auto Fragment = E->getFragmentInfo();
  if (!Fragment)
    return;

  // The frontend helps out GDB by emitting the members of local anonymous
  // unions as artificial local variables with shared storage. When SROA splits
  // the storage for artificial local variables that are smaller than the entire
  // union, the overhang piece will be outside of the allotted space for the
  // variable and this check fails.
  // FIXME: Remove this check as soon as clang stops doing this; it hides bugs.
  if (V->isArtificial())
    return;

  verifyFragmentExpression(*V, *Fragment, &DVR);
}

template <typename ValueOrMetadata>
void Verifier::verifyFragmentExpression(const DIVariable &V,
                                        DIExpression::FragmentInfo Fragment,
                                        ValueOrMetadata *Desc) {
  // If there's no size, the type is broken, but that should be checked
  // elsewhere.
  auto VarSize = V.getSizeInBits();
  if (!VarSize)
    return;

  unsigned FragSize = Fragment.SizeInBits;
  unsigned FragOffset = Fragment.OffsetInBits;
  CheckDI(FragSize + FragOffset <= *VarSize,
          "fragment is larger than or outside of variable", Desc, &V);
  CheckDI(FragSize != *VarSize, "fragment covers entire variable", Desc, &V);
}

void Verifier::verifyFnArgs(const DbgVariableIntrinsic &I) {
  // This function does not take the scope of noninlined function arguments into
  // account. Don't run it if current function is nodebug, because it may
  // contain inlined debug intrinsics.
  if (!HasDebugInfo)
    return;

  // For performance reasons only check non-inlined ones.
  if (I.getDebugLoc()->getInlinedAt())
    return;

  DILocalVariable *Var = I.getVariable();
  CheckDI(Var, "dbg intrinsic without variable");

  unsigned ArgNo = Var->getArg();
  if (!ArgNo)
    return;

  // Verify there are no duplicate function argument debug info entries.
  // These will cause hard-to-debug assertions in the DWARF backend.
  if (DebugFnArgs.size() < ArgNo)
    DebugFnArgs.resize(ArgNo, nullptr);

  auto *Prev = DebugFnArgs[ArgNo - 1];
  DebugFnArgs[ArgNo - 1] = Var;
  CheckDI(!Prev || (Prev == Var), "conflicting debug info for argument", &I,
          Prev, Var);
}
void Verifier::verifyFnArgs(const DbgVariableRecord &DVR) {
  // This function does not take the scope of noninlined function arguments into
  // account. Don't run it if current function is nodebug, because it may
  // contain inlined debug intrinsics.
  if (!HasDebugInfo)
    return;

  // For performance reasons only check non-inlined ones.
  if (DVR.getDebugLoc()->getInlinedAt())
    return;

  DILocalVariable *Var = DVR.getVariable();
  CheckDI(Var, "#dbg record without variable");

  unsigned ArgNo = Var->getArg();
  if (!ArgNo)
    return;

  // Verify there are no duplicate function argument debug info entries.
  // These will cause hard-to-debug assertions in the DWARF backend.
  if (DebugFnArgs.size() < ArgNo)
    DebugFnArgs.resize(ArgNo, nullptr);

  auto *Prev = DebugFnArgs[ArgNo - 1];
  DebugFnArgs[ArgNo - 1] = Var;
  CheckDI(!Prev || (Prev == Var), "conflicting debug info for argument", &DVR,
          Prev, Var);
}

void Verifier::verifyNotEntryValue(const DbgVariableIntrinsic &I) {
  DIExpression *E = dyn_cast_or_null<DIExpression>(I.getRawExpression());

  // We don't know whether this intrinsic verified correctly.
  if (!E || !E->isValid())
    return;

  if (isa<ValueAsMetadata>(I.getRawLocation())) {
    Value *VarValue = I.getVariableLocationOp(0);
    if (isa<UndefValue>(VarValue) || isa<PoisonValue>(VarValue))
      return;
    // We allow EntryValues for swift async arguments, as they have an
    // ABI-guarantee to be turned into a specific register.
    if (auto *ArgLoc = dyn_cast_or_null<Argument>(VarValue);
        ArgLoc && ArgLoc->hasAttribute(Attribute::SwiftAsync))
      return;
  }

  CheckDI(!E->isEntryValue(),
          "Entry values are only allowed in MIR unless they target a "
          "swiftasync Argument",
          &I);
}
void Verifier::verifyNotEntryValue(const DbgVariableRecord &DVR) {
  DIExpression *E = dyn_cast_or_null<DIExpression>(DVR.getRawExpression());

  // We don't know whether this intrinsic verified correctly.
  if (!E || !E->isValid())
    return;

  if (isa<ValueAsMetadata>(DVR.getRawLocation())) {
    Value *VarValue = DVR.getVariableLocationOp(0);
    if (isa<UndefValue>(VarValue) || isa<PoisonValue>(VarValue))
      return;
    // We allow EntryValues for swift async arguments, as they have an
    // ABI-guarantee to be turned into a specific register.
    if (auto *ArgLoc = dyn_cast_or_null<Argument>(VarValue);
        ArgLoc && ArgLoc->hasAttribute(Attribute::SwiftAsync))
      return;
  }

  CheckDI(!E->isEntryValue(),
          "Entry values are only allowed in MIR unless they target a "
          "swiftasync Argument",
          &DVR);
}

void Verifier::verifyCompileUnits() {
  // When more than one Module is imported into the same context, such as during
  // an LTO build before linking the modules, ODR type uniquing may cause types
  // to point to a different CU. This check does not make sense in this case.
  if (M.getContext().isODRUniquingDebugTypes())
    return;
  auto *CUs = M.getNamedMetadata("llvm.dbg.cu");
  SmallPtrSet<const Metadata *, 2> Listed;
  if (CUs)
    Listed.insert(CUs->op_begin(), CUs->op_end());
  for (const auto *CU : CUVisited)
    CheckDI(Listed.count(CU), "DICompileUnit not listed in llvm.dbg.cu", CU);
  CUVisited.clear();
}

void Verifier::verifyDeoptimizeCallingConvs() {
  if (DeoptimizeDeclarations.empty())
    return;

  const Function *First = DeoptimizeDeclarations[0];
  for (const auto *F : ArrayRef(DeoptimizeDeclarations).slice(1)) {
    Check(First->getCallingConv() == F->getCallingConv(),
          "All llvm.experimental.deoptimize declarations must have the same "
          "calling convention",
          First, F);
  }
}

void Verifier::verifyAttachedCallBundle(const CallBase &Call,
                                        const OperandBundleUse &BU) {
  FunctionType *FTy = Call.getFunctionType();

  Check((FTy->getReturnType()->isPointerTy() ||
         (Call.doesNotReturn() && FTy->getReturnType()->isVoidTy())),
        "a call with operand bundle \"clang.arc.attachedcall\" must call a "
        "function returning a pointer or a non-returning function that has a "
        "void return type",
        Call);

  Check(BU.Inputs.size() == 1 && isa<Function>(BU.Inputs.front()),
        "operand bundle \"clang.arc.attachedcall\" requires one function as "
        "an argument",
        Call);

  auto *Fn = cast<Function>(BU.Inputs.front());
  Intrinsic::ID IID = Fn->getIntrinsicID();

  if (IID) {
    Check((IID == Intrinsic::objc_retainAutoreleasedReturnValue ||
           IID == Intrinsic::objc_unsafeClaimAutoreleasedReturnValue),
          "invalid function argument", Call);
  } else {
    StringRef FnName = Fn->getName();
    Check((FnName == "objc_retainAutoreleasedReturnValue" ||
           FnName == "objc_unsafeClaimAutoreleasedReturnValue"),
          "invalid function argument", Call);
  }
}

void Verifier::verifyNoAliasScopeDecl() {
  if (NoAliasScopeDecls.empty())
    return;

  // only a single scope must be declared at a time.
  for (auto *II : NoAliasScopeDecls) {
    assert(II->getIntrinsicID() == Intrinsic::experimental_noalias_scope_decl &&
           "Not a llvm.experimental.noalias.scope.decl ?");
    const auto *ScopeListMV = dyn_cast<MetadataAsValue>(
        II->getOperand(Intrinsic::NoAliasScopeDeclScopeArg));
    Check(ScopeListMV != nullptr,
          "llvm.experimental.noalias.scope.decl must have a MetadataAsValue "
          "argument",
          II);

    const auto *ScopeListMD = dyn_cast<MDNode>(ScopeListMV->getMetadata());
    Check(ScopeListMD != nullptr, "!id.scope.list must point to an MDNode", II);
    Check(ScopeListMD->getNumOperands() == 1,
          "!id.scope.list must point to a list with a single scope", II);
    visitAliasScopeListMetadata(ScopeListMD);
  }

  // Only check the domination rule when requested. Once all passes have been
  // adapted this option can go away.
  if (!VerifyNoAliasScopeDomination)
    return;

  // Now sort the intrinsics based on the scope MDNode so that declarations of
  // the same scopes are next to each other.
  auto GetScope = [](IntrinsicInst *II) {
    const auto *ScopeListMV = cast<MetadataAsValue>(
        II->getOperand(Intrinsic::NoAliasScopeDeclScopeArg));
    return &cast<MDNode>(ScopeListMV->getMetadata())->getOperand(0);
  };

  // We are sorting on MDNode pointers here. For valid input IR this is ok.
  // TODO: Sort on Metadata ID to avoid non-deterministic error messages.
  auto Compare = [GetScope](IntrinsicInst *Lhs, IntrinsicInst *Rhs) {
    return GetScope(Lhs) < GetScope(Rhs);
  };

  llvm::sort(NoAliasScopeDecls, Compare);

  // Go over the intrinsics and check that for the same scope, they are not
  // dominating each other.
  auto ItCurrent = NoAliasScopeDecls.begin();
  while (ItCurrent != NoAliasScopeDecls.end()) {
    auto CurScope = GetScope(*ItCurrent);
    auto ItNext = ItCurrent;
    do {
      ++ItNext;
    } while (ItNext != NoAliasScopeDecls.end() &&
             GetScope(*ItNext) == CurScope);

    // [ItCurrent, ItNext) represents the declarations for the same scope.
    // Ensure they are not dominating each other.. but only if it is not too
    // expensive.
    if (ItNext - ItCurrent < 32)
      for (auto *I : llvm::make_range(ItCurrent, ItNext))
        for (auto *J : llvm::make_range(ItCurrent, ItNext))
          if (I != J)
            Check(!DT.dominates(I, J),
                  "llvm.experimental.noalias.scope.decl dominates another one "
                  "with the same scope",
                  I);
    ItCurrent = ItNext;
  }
}

//===----------------------------------------------------------------------===//
//  Implement the public interfaces to this file...
//===----------------------------------------------------------------------===//

bool llvm::verifyFunction(const Function &f, raw_ostream *OS) {
  Function &F = const_cast<Function &>(f);

  // Don't use a raw_null_ostream.  Printing IR is expensive.
  Verifier V(OS, /*ShouldTreatBrokenDebugInfoAsError=*/true, *f.getParent());

  // Note that this function's return value is inverted from what you would
  // expect of a function called "verify".
  return !V.verify(F);
}

bool llvm::verifyModule(const Module &M, raw_ostream *OS,
                        bool *BrokenDebugInfo) {
  // Don't use a raw_null_ostream.  Printing IR is expensive.
  Verifier V(OS, /*ShouldTreatBrokenDebugInfoAsError=*/!BrokenDebugInfo, M);

  bool Broken = false;
  for (const Function &F : M)
    Broken |= !V.verify(F);

  Broken |= !V.verify();
  if (BrokenDebugInfo)
    *BrokenDebugInfo = V.hasBrokenDebugInfo();
  // Note that this function's return value is inverted from what you would
  // expect of a function called "verify".
  return Broken;
}

namespace {

struct VerifierLegacyPass : public FunctionPass {
  static char ID;

  std::unique_ptr<Verifier> V;
  bool FatalErrors = true;

  VerifierLegacyPass() : FunctionPass(ID) {
    initializeVerifierLegacyPassPass(*PassRegistry::getPassRegistry());
  }
  explicit VerifierLegacyPass(bool FatalErrors)
      : FunctionPass(ID),
        FatalErrors(FatalErrors) {
    initializeVerifierLegacyPassPass(*PassRegistry::getPassRegistry());
  }

  bool doInitialization(Module &M) override {
    V = std::make_unique<Verifier>(
        &dbgs(), /*ShouldTreatBrokenDebugInfoAsError=*/false, M);
    return false;
  }

  bool runOnFunction(Function &F) override {
    if (!V->verify(F) && FatalErrors) {
      errs() << "in function " << F.getName() << '\n';
      report_fatal_error("Broken function found, compilation aborted!");
    }
    return false;
  }

  bool doFinalization(Module &M) override {
    bool HasErrors = false;
    for (Function &F : M)
      if (F.isDeclaration())
        HasErrors |= !V->verify(F);

    HasErrors |= !V->verify();
    if (FatalErrors && (HasErrors || V->hasBrokenDebugInfo()))
      report_fatal_error("Broken module found, compilation aborted!");
    return false;
  }

  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};

} // end anonymous namespace

/// Helper to issue failure from the TBAA verification
template <typename... Tys> void TBAAVerifier::CheckFailed(Tys &&... Args) {
  if (Diagnostic)
    return Diagnostic->CheckFailed(Args...);
}

#define CheckTBAA(C, ...)                                                      \
  do {                                                                         \
    if (!(C)) {                                                                \
      CheckFailed(__VA_ARGS__);                                                \
      return false;                                                            \
    }                                                                          \
  } while (false)

/// Verify that \p BaseNode can be used as the "base type" in the struct-path
/// TBAA scheme.  This means \p BaseNode is either a scalar node, or a
/// struct-type node describing an aggregate data structure (like a struct).
TBAAVerifier::TBAABaseNodeSummary
TBAAVerifier::verifyTBAABaseNode(Instruction &I, const MDNode *BaseNode,
                                 bool IsNewFormat) {
  if (BaseNode->getNumOperands() < 2) {
    CheckFailed("Base nodes must have at least two operands", &I, BaseNode);
    return {true, ~0u};
  }

  auto Itr = TBAABaseNodes.find(BaseNode);
  if (Itr != TBAABaseNodes.end())
    return Itr->second;

  auto Result = verifyTBAABaseNodeImpl(I, BaseNode, IsNewFormat);
  auto InsertResult = TBAABaseNodes.insert({BaseNode, Result});
  (void)InsertResult;
  assert(InsertResult.second && "We just checked!");
  return Result;
}

TBAAVerifier::TBAABaseNodeSummary
TBAAVerifier::verifyTBAABaseNodeImpl(Instruction &I, const MDNode *BaseNode,
                                     bool IsNewFormat) {
  const TBAAVerifier::TBAABaseNodeSummary InvalidNode = {true, ~0u};

  if (BaseNode->getNumOperands() == 2) {
    // Scalar nodes can only be accessed at offset 0.
    return isValidScalarTBAANode(BaseNode)
               ? TBAAVerifier::TBAABaseNodeSummary({false, 0})
               : InvalidNode;
  }

  if (IsNewFormat) {
    if (BaseNode->getNumOperands() % 3 != 0) {
      CheckFailed("Access tag nodes must have the number of operands that is a "
                  "multiple of 3!", BaseNode);
      return InvalidNode;
    }
  } else {
    if (BaseNode->getNumOperands() % 2 != 1) {
      CheckFailed("Struct tag nodes must have an odd number of operands!",
                  BaseNode);
      return InvalidNode;
    }
  }

  // Check the type size field.
  if (IsNewFormat) {
    auto *TypeSizeNode = mdconst::dyn_extract_or_null<ConstantInt>(
        BaseNode->getOperand(1));
    if (!TypeSizeNode) {
      CheckFailed("Type size nodes must be constants!", &I, BaseNode);
      return InvalidNode;
    }
  }

  // Check the type name field. In the new format it can be anything.
  if (!IsNewFormat && !isa<MDString>(BaseNode->getOperand(0))) {
    CheckFailed("Struct tag nodes have a string as their first operand",
                BaseNode);
    return InvalidNode;
  }

  bool Failed = false;

  std::optional<APInt> PrevOffset;
  unsigned BitWidth = ~0u;

  // We've already checked that BaseNode is not a degenerate root node with one
  // operand in \c verifyTBAABaseNode, so this loop should run at least once.
  unsigned FirstFieldOpNo = IsNewFormat ? 3 : 1;
  unsigned NumOpsPerField = IsNewFormat ? 3 : 2;
  for (unsigned Idx = FirstFieldOpNo; Idx < BaseNode->getNumOperands();
           Idx += NumOpsPerField) {
    const MDOperand &FieldTy = BaseNode->getOperand(Idx);
    const MDOperand &FieldOffset = BaseNode->getOperand(Idx + 1);
    if (!isa<MDNode>(FieldTy)) {
      CheckFailed("Incorrect field entry in struct type node!", &I, BaseNode);
      Failed = true;
      continue;
    }

    auto *OffsetEntryCI =
        mdconst::dyn_extract_or_null<ConstantInt>(FieldOffset);
    if (!OffsetEntryCI) {
      CheckFailed("Offset entries must be constants!", &I, BaseNode);
      Failed = true;
      continue;
    }

    if (BitWidth == ~0u)
      BitWidth = OffsetEntryCI->getBitWidth();

    if (OffsetEntryCI->getBitWidth() != BitWidth) {
      CheckFailed(
          "Bitwidth between the offsets and struct type entries must match", &I,
          BaseNode);
      Failed = true;
      continue;
    }

    // NB! As far as I can tell, we generate a non-strictly increasing offset
    // sequence only from structs that have zero size bit fields.  When
    // recursing into a contained struct in \c getFieldNodeFromTBAABaseNode we
    // pick the field lexically the latest in struct type metadata node.  This
    // mirrors the actual behavior of the alias analysis implementation.
    bool IsAscending =
        !PrevOffset || PrevOffset->ule(OffsetEntryCI->getValue());

    if (!IsAscending) {
      CheckFailed("Offsets must be increasing!", &I, BaseNode);
      Failed = true;
    }

    PrevOffset = OffsetEntryCI->getValue();

    if (IsNewFormat) {
      auto *MemberSizeNode = mdconst::dyn_extract_or_null<ConstantInt>(
          BaseNode->getOperand(Idx + 2));
      if (!MemberSizeNode) {
        CheckFailed("Member size entries must be constants!", &I, BaseNode);
        Failed = true;
        continue;
      }
    }
  }

  return Failed ? InvalidNode
                : TBAAVerifier::TBAABaseNodeSummary(false, BitWidth);
}

static bool IsRootTBAANode(const MDNode *MD) {
  return MD->getNumOperands() < 2;
}

static bool IsScalarTBAANodeImpl(const MDNode *MD,
                                 SmallPtrSetImpl<const MDNode *> &Visited) {
  if (MD->getNumOperands() != 2 && MD->getNumOperands() != 3)
    return false;

  if (!isa<MDString>(MD->getOperand(0)))
    return false;

  if (MD->getNumOperands() == 3) {
    auto *Offset = mdconst::dyn_extract<ConstantInt>(MD->getOperand(2));
    if (!(Offset && Offset->isZero() && isa<MDString>(MD->getOperand(0))))
      return false;
  }

  auto *Parent = dyn_cast_or_null<MDNode>(MD->getOperand(1));
  return Parent && Visited.insert(Parent).second &&
         (IsRootTBAANode(Parent) || IsScalarTBAANodeImpl(Parent, Visited));
}

bool TBAAVerifier::isValidScalarTBAANode(const MDNode *MD) {
  auto ResultIt = TBAAScalarNodes.find(MD);
  if (ResultIt != TBAAScalarNodes.end())
    return ResultIt->second;

  SmallPtrSet<const MDNode *, 4> Visited;
  bool Result = IsScalarTBAANodeImpl(MD, Visited);
  auto InsertResult = TBAAScalarNodes.insert({MD, Result});
  (void)InsertResult;
  assert(InsertResult.second && "Just checked!");

  return Result;
}

/// Returns the field node at the offset \p Offset in \p BaseNode.  Update \p
/// Offset in place to be the offset within the field node returned.
///
/// We assume we've okayed \p BaseNode via \c verifyTBAABaseNode.
MDNode *TBAAVerifier::getFieldNodeFromTBAABaseNode(Instruction &I,
                                                   const MDNode *BaseNode,
                                                   APInt &Offset,
                                                   bool IsNewFormat) {
  assert(BaseNode->getNumOperands() >= 2 && "Invalid base node!");

  // Scalar nodes have only one possible "field" -- their parent in the access
  // hierarchy.  Offset must be zero at this point, but our caller is supposed
  // to check that.
  if (BaseNode->getNumOperands() == 2)
    return cast<MDNode>(BaseNode->getOperand(1));

  unsigned FirstFieldOpNo = IsNewFormat ? 3 : 1;
  unsigned NumOpsPerField = IsNewFormat ? 3 : 2;
  for (unsigned Idx = FirstFieldOpNo; Idx < BaseNode->getNumOperands();
           Idx += NumOpsPerField) {
    auto *OffsetEntryCI =
        mdconst::extract<ConstantInt>(BaseNode->getOperand(Idx + 1));
    if (OffsetEntryCI->getValue().ugt(Offset)) {
      if (Idx == FirstFieldOpNo) {
        CheckFailed("Could not find TBAA parent in struct type node", &I,
                    BaseNode, &Offset);
        return nullptr;
      }

      unsigned PrevIdx = Idx - NumOpsPerField;
      auto *PrevOffsetEntryCI =
          mdconst::extract<ConstantInt>(BaseNode->getOperand(PrevIdx + 1));
      Offset -= PrevOffsetEntryCI->getValue();
      return cast<MDNode>(BaseNode->getOperand(PrevIdx));
    }
  }

  unsigned LastIdx = BaseNode->getNumOperands() - NumOpsPerField;
  auto *LastOffsetEntryCI = mdconst::extract<ConstantInt>(
      BaseNode->getOperand(LastIdx + 1));
  Offset -= LastOffsetEntryCI->getValue();
  return cast<MDNode>(BaseNode->getOperand(LastIdx));
}

static bool isNewFormatTBAATypeNode(llvm::MDNode *Type) {
  if (!Type || Type->getNumOperands() < 3)
    return false;

  // In the new format type nodes shall have a reference to the parent type as
  // its first operand.
  return isa_and_nonnull<MDNode>(Type->getOperand(0));
}

bool TBAAVerifier::visitTBAAMetadata(Instruction &I, const MDNode *MD) {
  CheckTBAA(MD->getNumOperands() > 0, "TBAA metadata cannot have 0 operands",
            &I, MD);

  CheckTBAA(isa<LoadInst>(I) || isa<StoreInst>(I) || isa<CallInst>(I) ||
                isa<VAArgInst>(I) || isa<AtomicRMWInst>(I) ||
                isa<AtomicCmpXchgInst>(I),
            "This instruction shall not have a TBAA access tag!", &I);

  bool IsStructPathTBAA =
      isa<MDNode>(MD->getOperand(0)) && MD->getNumOperands() >= 3;

  CheckTBAA(IsStructPathTBAA,
            "Old-style TBAA is no longer allowed, use struct-path TBAA instead",
            &I);

  MDNode *BaseNode = dyn_cast_or_null<MDNode>(MD->getOperand(0));
  MDNode *AccessType = dyn_cast_or_null<MDNode>(MD->getOperand(1));

  bool IsNewFormat = isNewFormatTBAATypeNode(AccessType);

  if (IsNewFormat) {
    CheckTBAA(MD->getNumOperands() == 4 || MD->getNumOperands() == 5,
              "Access tag metadata must have either 4 or 5 operands", &I, MD);
  } else {
    CheckTBAA(MD->getNumOperands() < 5,
              "Struct tag metadata must have either 3 or 4 operands", &I, MD);
  }

  // Check the access size field.
  if (IsNewFormat) {
    auto *AccessSizeNode = mdconst::dyn_extract_or_null<ConstantInt>(
        MD->getOperand(3));
    CheckTBAA(AccessSizeNode, "Access size field must be a constant", &I, MD);
  }

  // Check the immutability flag.
  unsigned ImmutabilityFlagOpNo = IsNewFormat ? 4 : 3;
  if (MD->getNumOperands() == ImmutabilityFlagOpNo + 1) {
    auto *IsImmutableCI = mdconst::dyn_extract_or_null<ConstantInt>(
        MD->getOperand(ImmutabilityFlagOpNo));
    CheckTBAA(IsImmutableCI,
              "Immutability tag on struct tag metadata must be a constant", &I,
              MD);
    CheckTBAA(
        IsImmutableCI->isZero() || IsImmutableCI->isOne(),
        "Immutability part of the struct tag metadata must be either 0 or 1",
        &I, MD);
  }

  CheckTBAA(BaseNode && AccessType,
            "Malformed struct tag metadata: base and access-type "
            "should be non-null and point to Metadata nodes",
            &I, MD, BaseNode, AccessType);

  if (!IsNewFormat) {
    CheckTBAA(isValidScalarTBAANode(AccessType),
              "Access type node must be a valid scalar type", &I, MD,
              AccessType);
  }

  auto *OffsetCI = mdconst::dyn_extract_or_null<ConstantInt>(MD->getOperand(2));
  CheckTBAA(OffsetCI, "Offset must be constant integer", &I, MD);

  APInt Offset = OffsetCI->getValue();
  bool SeenAccessTypeInPath = false;

  SmallPtrSet<MDNode *, 4> StructPath;

  for (/* empty */; BaseNode && !IsRootTBAANode(BaseNode);
       BaseNode = getFieldNodeFromTBAABaseNode(I, BaseNode, Offset,
                                               IsNewFormat)) {
    if (!StructPath.insert(BaseNode).second) {
      CheckFailed("Cycle detected in struct path", &I, MD);
      return false;
    }

    bool Invalid;
    unsigned BaseNodeBitWidth;
    std::tie(Invalid, BaseNodeBitWidth) = verifyTBAABaseNode(I, BaseNode,
                                                             IsNewFormat);

    // If the base node is invalid in itself, then we've already printed all the
    // errors we wanted to print.
    if (Invalid)
      return false;

    SeenAccessTypeInPath |= BaseNode == AccessType;

    if (isValidScalarTBAANode(BaseNode) || BaseNode == AccessType)
      CheckTBAA(Offset == 0, "Offset not zero at the point of scalar access",
                &I, MD, &Offset);

    CheckTBAA(BaseNodeBitWidth == Offset.getBitWidth() ||
                  (BaseNodeBitWidth == 0 && Offset == 0) ||
                  (IsNewFormat && BaseNodeBitWidth == ~0u),
              "Access bit-width not the same as description bit-width", &I, MD,
              BaseNodeBitWidth, Offset.getBitWidth());

    if (IsNewFormat && SeenAccessTypeInPath)
      break;
  }

  CheckTBAA(SeenAccessTypeInPath, "Did not see access type in access path!", &I,
            MD);
  return true;
}

char VerifierLegacyPass::ID = 0;
INITIALIZE_PASS(VerifierLegacyPass, "verify", "Module Verifier", false, false)

FunctionPass *llvm::createVerifierPass(bool FatalErrors) {
  return new VerifierLegacyPass(FatalErrors);
}

AnalysisKey VerifierAnalysis::Key;
VerifierAnalysis::Result VerifierAnalysis::run(Module &M,
                                               ModuleAnalysisManager &) {
  Result Res;
  Res.IRBroken = llvm::verifyModule(M, &dbgs(), &Res.DebugInfoBroken);
  return Res;
}

VerifierAnalysis::Result VerifierAnalysis::run(Function &F,
                                               FunctionAnalysisManager &) {
  return { llvm::verifyFunction(F, &dbgs()), false };
}

PreservedAnalyses VerifierPass::run(Module &M, ModuleAnalysisManager &AM) {
  auto Res = AM.getResult<VerifierAnalysis>(M);
  if (FatalErrors && (Res.IRBroken || Res.DebugInfoBroken))
    report_fatal_error("Broken module found, compilation aborted!");

  return PreservedAnalyses::all();
}

PreservedAnalyses VerifierPass::run(Function &F, FunctionAnalysisManager &AM) {
  auto res = AM.getResult<VerifierAnalysis>(F);
  if (res.IRBroken && FatalErrors)
    report_fatal_error("Broken function found, compilation aborted!");

  return PreservedAnalyses::all();
}
